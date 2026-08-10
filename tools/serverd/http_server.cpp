#include "tools/serverd/http_server.h"

#if defined(OPENADS_WITH_HTTP)

#include "tools/serverd/spa_index.h"

#include "engine/data_dict.h"
#include "mgmt/error_log.h"
#include "network/server.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using nlohmann::json;
namespace fs = std::filesystem;

namespace openads::studio {

namespace {

// Open a fresh ABI connection per request. Stateless web requests
// + per-request connection is the simplest correctness model:
// the engine's existing locking handles concurrent connections.
struct AbiSession {
    ADSHANDLE conn = 0;
    explicit AbiSession(const std::string& dir) {
        std::vector<UNSIGNED8> buf(dir.size() + 1);
        std::memcpy(buf.data(), dir.c_str(), dir.size() + 1);
        AdsConnect60(buf.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &conn);
    }
    ~AbiSession() {
        if (conn != 0) AdsDisconnect(conn);
    }
    bool ok() const noexcept { return conn != 0; }
};

// studio.web.0.13 — figure out the right ADS_* table-type code
// for a given DBF + sidecar layout. Default is CDX (FoxPro-style)
// when no overrides apply; an `.ntx` next to the DBF flips to
// Clipper-style NTX; the explicit `type` query param wins over
// either.
UNSIGNED16 resolve_table_type(const std::string& dir,
                               const std::string& tname,
                               const std::string& override) {
    std::string ov = override;
    std::transform(ov.begin(), ov.end(), ov.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    if (ov == "ntx") return ADS_NTX;
    if (ov == "cdx") return ADS_CDX;
    if (ov == "adt") return ADS_ADT;
    if (ov == "vfp") return ADS_VFP;
    if (!ov.empty()) return ADS_CDX;
    fs::path stem = (fs::path(dir) / tname).parent_path() /
                    fs::path(tname).stem();
    std::error_code ec;
    fs::path ntx = stem; ntx += ".ntx";
    fs::path cdx = stem; cdx += ".cdx";
    if (fs::exists(ntx, ec) && !fs::exists(cdx, ec)) return ADS_NTX;
    return ADS_CDX;
}

json json_error(const std::string& msg, int http_code) {
    return json{{"error", msg}, {"http_code", http_code}};
}

// studio.web.0.15 — Studio's row serialiser used to push raw memo
// bytes into a JSON string, which throws json.exception.type_error.316
// the moment the payload contained any non-UTF-8 byte (e.g. a ZIP /
// JPG / encrypted blob stored in an M field). The helpers below let
// `table_rows` emit:
//   - a normal string when the bytes ARE valid UTF-8, OR
//   - an object {"_b64": "<base64>", "_size": N, "_truncated": bool}
//     for binary memos (with a 1 KB preview cap so the JSON response
//     stays bounded even for multi-MB blobs).
bool is_valid_utf8(const std::uint8_t* p, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        std::uint8_t c = p[i];
        if (c < 0x80) { ++i; continue; }
        std::size_t need;
        std::uint8_t lo, hi;
        if      ((c & 0xE0) == 0xC0) { need = 1; lo = 0x80; hi = 0xBF;
                                       if (c < 0xC2) return false; }
        else if ((c & 0xF0) == 0xE0) { need = 2; lo = 0x80; hi = 0xBF; }
        else if ((c & 0xF8) == 0xF0) { need = 3; lo = 0x80; hi = 0xBF;
                                       if (c > 0xF4) return false; }
        else return false;
        if (i + need >= n) return false;
        for (std::size_t k = 1; k <= need; ++k) {
            std::uint8_t cc = p[i + k];
            std::uint8_t llo = (k == 1 && c == 0xE0) ? 0xA0 :
                                (k == 1 && c == 0xED) ? 0x80 :
                                (k == 1 && c == 0xF0) ? 0x90 :
                                (k == 1 && c == 0xF4) ? 0x80 : lo;
            std::uint8_t hhi = (k == 1 && c == 0xED) ? 0x9F :
                                (k == 1 && c == 0xF4) ? 0x8F : hi;
            if (cc < llo || cc > hhi) return false;
        }
        i += need + 1;
    }
    return true;
}

std::string base64_encode(const std::uint8_t* p, std::size_t n) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= n) {
        std::uint32_t v = (std::uint32_t(p[i]) << 16) |
                          (std::uint32_t(p[i + 1]) << 8) |
                           std::uint32_t(p[i + 2]);
        out.push_back(T[(v >> 18) & 0x3F]);
        out.push_back(T[(v >> 12) & 0x3F]);
        out.push_back(T[(v >>  6) & 0x3F]);
        out.push_back(T[ v        & 0x3F]);
        i += 3;
    }
    if (i < n) {
        std::uint32_t v = std::uint32_t(p[i]) << 16;
        if (i + 1 < n) v |= std::uint32_t(p[i + 1]) << 8;
        out.push_back(T[(v >> 18) & 0x3F]);
        out.push_back(T[(v >> 12) & 0x3F]);
        out.push_back(i + 1 < n ? T[(v >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

constexpr std::size_t STUDIO_BIN_PREVIEW_BYTES = 1024;

// Map ADS_* numeric field-type codes to single-letter dBASE / xBase
// labels so Studio's schema view can display "C" / "N" / "M" / "D"
// instead of the raw integers (which are meaningless to anyone who
// hasn't memorised ace.h).
const char* ads_type_letter(UNSIGNED16 t) {
    // Numeric values per include/openads/ace.h:
    //   1 LOGICAL  2 NUMERIC  3 DATE  4 STRING  5 MEMO  6 BINARY/RAW
    //   7 IMAGE   11 INTEGER 13 TIME 14 TIMESTAMP 15 AUTOINC 28 NMEMO
    switch (t) {
        case ADS_LOGICAL:    return "L";
        case ADS_NUMERIC:    return "N";
        case ADS_DATE:       return "D";
        case ADS_STRING:     return "C";
        case ADS_MEMO:       return "M";
        case ADS_BINARY:     return "B";   // alias ADS_RAW
        case ADS_IMAGE:      return "P";   // memo picture
        case ADS_INTEGER:    return "I";
        case ADS_TIME:       return "T";
        case ADS_TIMESTAMP:  return "@";
        case ADS_AUTOINC:    return "+";
        case ADS_NMEMO:      return "M";
        default: break;
    }
    return "?";
}

json bytes_to_json_cell(const std::uint8_t* p, std::size_t n) {
    if (is_valid_utf8(p, n)) {
        // Trim trailing spaces (legacy Studio behaviour for fixed-
        // width C / N fields) but only when the payload is text.
        while (n > 0 && p[n - 1] == ' ') --n;
        return std::string(reinterpret_cast<const char*>(p), n);
    }
    std::size_t preview = n < STUDIO_BIN_PREVIEW_BYTES
        ? n : STUDIO_BIN_PREVIEW_BYTES;
    return json{
        {"_b64",       base64_encode(p, preview)},
        {"_size",      static_cast<std::uint64_t>(n)},
        {"_truncated", n > preview}
    };
}

// List `*.dbf` files in the data dir, including tables below subdirectories.
// Return paths relative to the configured data root so the Studio can show
// the actual folder layout and pass the same name back to the table APIs.
std::vector<std::string> list_dbf_files(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".dbf")
            out.push_back(fs::relative(e.path(), fs::path(dir), ec).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// studio.web.0.2 — schema for a single table:
// returns column metadata (name, type letter, length, decimals)
// + record count + raw file size on disk.
json table_schema(const std::string& dir, const std::string& tname,
                  const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t n = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), n);
    ADSHANDLE hTable = 0;
    UNSIGNED16 ttype = resolve_table_type(dir, tname, type_ov);
    if (AdsOpenTable(sess.conn, leaf, nullptr, ttype,
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED16 nf = 0;
    AdsGetNumFields(hTable, &nf);
    json cols = json::array();
    for (UNSIGNED16 i = 1; i <= nf; ++i) {
        UNSIGNED8 nm[64] = {0};
        UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hTable, i, nm, &cap);
        std::string fname(reinterpret_cast<char*>(nm), cap);
        UNSIGNED16 ftype = 0;
        UNSIGNED32 flen  = 0;
        UNSIGNED16 fdec  = 0;
        std::vector<UNSIGNED8> fbuf(fname.size() + 1);
        std::memcpy(fbuf.data(), fname.data(), fname.size());
        AdsGetFieldType    (hTable, fbuf.data(), &ftype);
        AdsGetFieldLength  (hTable, fbuf.data(), &flen);
        AdsGetFieldDecimals(hTable, fbuf.data(), &fdec);
        cols.push_back(json{
            {"name",     fname},
            {"type",     ftype},
            {"type_name", ads_type_letter(ftype)},
            {"length",   flen},
            {"decimals", fdec}
        });
    }
    UNSIGNED32 rc = 0;
    AdsGetRecordCount(hTable, 0, &rc);
    AdsCloseTable(hTable);
    std::error_code ec;
    auto sz = fs::file_size(fs::path(dir) / tname, ec);
    return json{
        {"table",        tname},
        {"columns",      cols},
        {"record_count", rc},
        {"file_bytes",   ec ? 0 : static_cast<std::uint64_t>(sz)}
    };
}

// studio.web.0.2 — paginated row browse for a single table.
json table_rows(const std::string& dir, const std::string& tname,
                std::uint32_t offset, std::uint32_t limit,
                const std::string& type_ov = "",
                const std::string& aof_cond = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }

    // studio.web.0.18 — Rushmore-style AOF filter applied here. If
    // the caller passed `?aof=<cond>`, install it via AdsSetAOF;
    // Skip / GoTop afterwards walk only the matching records, and
    // AdsGetAOFOptLevel reports back FULL / PART / NONE so the SPA
    // can render an explicit "served by index" badge.
    std::string aof_level = "NONE";
    std::string aof_error;
    bool aof_active = false;
    if (!aof_cond.empty()) {
        std::vector<UNSIGNED8> c(aof_cond.size() + 1);
        std::memcpy(c.data(), aof_cond.data(), aof_cond.size());
        if (AdsSetAOF(hTable, c.data(), 0) != 0) {
            aof_error = "AdsSetAOF rejected the condition (parse error or "
                        "unsupported expression)";
        } else {
            aof_active = true;
            UNSIGNED16 lvl = 0; UNSIGNED16 buflen = 0;
            AdsGetAOFOptLevel(hTable, &lvl, nullptr, &buflen);
            aof_level = (lvl == ADS_OPTIMIZED_FULL) ? "FULL"
                      : (lvl == ADS_OPTIMIZED_PART) ? "PART"
                                                    : "NONE";
        }
    }
    UNSIGNED16 nf = 0;
    AdsGetNumFields(hTable, &nf);
    std::vector<std::string> cnames; cnames.reserve(nf);
    std::vector<UNSIGNED16> ctypes; ctypes.reserve(nf);
    for (UNSIGNED16 i = 1; i <= nf; ++i) {
        UNSIGNED8 nm[64] = {0}; UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hTable, i, nm, &cap);
        std::string fname(reinterpret_cast<char*>(nm), cap);
        UNSIGNED16 ftype = 0;
        std::vector<UNSIGNED8> fbuf(fname.size() + 1);
        std::memcpy(fbuf.data(), fname.data(), fname.size());
        AdsGetFieldType(hTable, fbuf.data(), &ftype);
        cnames.emplace_back(std::move(fname));
        ctypes.push_back(ftype);
    }
    UNSIGNED32 rc = 0;
    AdsGetRecordCount(hTable, 0, &rc);
    // studio.web.0.18 — expose col_types so the SPA AOF toolbar can
    // surface "Create index on <field>" hints only for the column
    // types V1's index-accelerated path actually serves (character /
    // memo). Numeric / date / logical fields stay on the per-record
    // fallback regardless of indexing in V1.
    json col_types = json::array();
    for (auto t : ctypes) col_types.push_back(t);
    json out{{"cols", cnames}, {"col_types", col_types},
             {"rows", json::array()},
             {"total", rc}, {"offset", offset}, {"limit", limit},
             {"aof_active", aof_active},
             {"aof_level",  aof_level}};
    if (!aof_error.empty()) out["aof_error"] = aof_error;
    if (offset >= rc) {
        AdsCloseTable(hTable);
        return out;
    }
    // With AOF active the bitmap drives navigation; GotoTop+Skip
    // walks only the visible set. Without AOF the legacy
    // GotoRecord(offset+1) path is preserved.
    if (aof_active) {
        AdsGotoTop(hTable);
        for (std::uint32_t i = 0; i < offset; ++i) {
            UNSIGNED16 atend = 0;
            AdsAtEOF(hTable, &atend);
            if (atend) break;
            AdsSkip(hTable, 1);
        }
    } else {
        AdsGotoRecord(hTable, offset + 1);
    }
    std::uint32_t walked = 0;
    while (walked < limit) {
        UNSIGNED16 atend = 0;
        AdsAtEOF(hTable, &atend);
        if (atend) break;
        UNSIGNED32 recno = 0;
        AdsGetRecordNum(hTable, 0, &recno);
        UNSIGNED16 deleted = 0;
        AdsIsRecordDeleted(hTable, &deleted);
        json row = json::array();
        row.push_back(json{{"_recno", recno},
                           {"_deleted", deleted != 0}});
        for (std::size_t i = 0; i < cnames.size(); ++i) {
            const auto& cn = cnames[i];
            std::vector<UNSIGNED8> fbuf(cn.size() + 1);
            std::memcpy(fbuf.data(), cn.data(), cn.size());

            const bool is_memo = (ctypes[i] == ADS_MEMO ||
                                  ctypes[i] == ADS_BINARY ||
                                  ctypes[i] == ADS_IMAGE ||
                                  ctypes[i] == ADS_NMEMO);
            UNSIGNED32 mlen = 0;
            if (is_memo) {
                if (AdsGetMemoLength(hTable, fbuf.data(), &mlen) != 0)
                    mlen = 0;
            }
            // Memo path: allocate exact size + 1; non-memo path: 4 KB
            // is plenty for fixed-width C / N / D / L / T / I / Y.
            std::uint32_t cap = is_memo ? (mlen + 1) : 4096u;
            std::vector<UNSIGNED8> vbuf(cap, 0);
            UNSIGNED32 vcap = cap;
            if (AdsGetField(hTable, fbuf.data(), vbuf.data(), &vcap, 0) != 0)
                vcap = 0;
            row.push_back(bytes_to_json_cell(vbuf.data(), vcap));
        }
        out["rows"].push_back(std::move(row));
        ++walked;
        if (AdsSkip(hTable, 1) != 0) break;
    }
    AdsCloseTable(hTable);
    return out;
}

// studio.web.0.2 — append a new row by JSON {col: value, ...}.
json table_insert(const std::string& dir, const std::string& tname,
                  const json& values,
                  const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsAppendRecord(hTable) != 0) {
        AdsCloseTable(hTable);
        return json_error("AdsAppendRecord failed", 500);
    }
    if (values.is_object()) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            std::string col = it.key();
            std::string val = it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump();
            std::vector<UNSIGNED8> fbuf(col.size() + 1);
            std::memcpy(fbuf.data(), col.data(), col.size());
            std::vector<UNSIGNED8> vbuf(val.size());
            if (!val.empty()) std::memcpy(vbuf.data(), val.data(), val.size());
            AdsSetString(hTable, fbuf.data(),
                         val.empty() ? nullptr : vbuf.data(),
                         static_cast<UNSIGNED32>(val.size()));
        }
    }
    AdsWriteRecord(hTable);
    UNSIGNED32 newrec = 0;
    AdsGetRecordNum(hTable, 0, &newrec);
    AdsCloseTable(hTable);
    return json{{"recno", newrec}, {"ok", true}};
}

// studio.web.0.2 — overwrite columns in an existing record.
json table_update(const std::string& dir, const std::string& tname,
                  std::uint32_t recno, const json& values,
                  const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsGotoRecord(hTable, recno) != 0) {
        AdsCloseTable(hTable);
        return json_error("recno out of range: " + std::to_string(recno), 404);
    }
    if (values.is_object()) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            std::string col = it.key();
            std::string val = it.value().is_string()
                ? it.value().get<std::string>()
                : it.value().dump();
            std::vector<UNSIGNED8> fbuf(col.size() + 1);
            std::memcpy(fbuf.data(), col.data(), col.size());
            std::vector<UNSIGNED8> vbuf(val.size());
            if (!val.empty()) std::memcpy(vbuf.data(), val.data(), val.size());
            AdsSetString(hTable, fbuf.data(),
                         val.empty() ? nullptr : vbuf.data(),
                         static_cast<UNSIGNED32>(val.size()));
        }
    }
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);
    return json{{"recno", recno}, {"ok", true}};
}

// studio.web.0.2 — mark deleted (Clipper convention) or recall.
json table_delete(const std::string& dir, const std::string& tname,
                  std::uint32_t recno, bool recall,
                  const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    if (AdsGotoRecord(hTable, recno) != 0) {
        AdsCloseTable(hTable);
        return json_error("recno out of range", 404);
    }
    if (recall) AdsRecallRecord(hTable);
    else        AdsDeleteRecord(hTable);
    AdsWriteRecord(hTable);
    AdsCloseTable(hTable);
    return json{{"recno", recno}, {"deleted", !recall}};
}

// studio.web.0.3 — CREATE TABLE through SQL DDL. Body:
//   { "name": "x.dbf", "columns": [{name, type, length, decimals?}, ...] }
// Type letters follow DBF convention (C, N, L, D, M, ...).
json table_create(const std::string& dir, const json& body) {
    if (!body.is_object() || !body.contains("name") ||
        !body.contains("columns") || !body["columns"].is_array() ||
        body["columns"].empty()) {
        return json_error("body must have {name, columns[]}", 400);
    }
    std::string name = body.value("name", "");
    if (name.empty()) return json_error("missing 'name'", 400);
    std::string sql = "CREATE TABLE " + name + " (";
    bool first = true;
    for (auto& c : body["columns"]) {
        if (!first) sql += ", ";
        first = false;
        std::string cn = c.value("name", "");
        std::string ct = c.value("type", "C");
        int cl = c.value("length", 10);
        int cd = c.value("decimals", 0);
        if (cn.empty() || ct.empty()) {
            return json_error("each column needs name + type", 400);
        }
        sql += cn + " " + ct + "(" + std::to_string(cl);
        if (cd > 0) sql += "," + std::to_string(cd);
        sql += ")";
    }
    sql += ")";

    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(sess.conn, &hStmt);
    std::vector<UNSIGNED8> sqlbuf(sql.size() + 1);
    std::memcpy(sqlbuf.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rrc = AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur);
    if (hCur != 0) AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    if (rrc != 0) return json_error("CREATE TABLE failed: " + sql, 400);
    return json{{"ok", true}, {"sql", sql}};
}

// studio.web.0.3 — DROP TABLE. Removes the .dbf + matching
// sidecars (.cdx / .ntx / .dbt / .fpt / .dbv / .lck) on disk.
json table_drop(const std::string& dir, const std::string& tname) {
    fs::path base = fs::path(dir) / tname;
    std::error_code ec;
    if (!fs::exists(base, ec)) {
        return json_error("table not found: " + tname, 404);
    }
    std::vector<std::string> sidecar_exts =
        {".cdx", ".ntx", ".dbt", ".fpt", ".dbv", ".lck"};
    int removed = 0;
    fs::remove(base, ec); ++removed;
    fs::path stem = base.parent_path() / base.stem();
    for (auto& ext : sidecar_exts) {
        fs::path p = stem; p += ext;
        if (fs::exists(p, ec)) {
            fs::remove(p, ec);
            ++removed;
        }
    }
    return json{{"ok", true}, {"removed", removed}, {"table", tname}};
}

// studio.web.0.3 — encrypt a DBF in place via AdsEncryptTable.
// Body: { "password": "..." }. Sets the connection encryption
// password first (M11.2), then encrypts the table.
json table_encrypt(const std::string& dir, const std::string& tname,
                   const json& body,
                   const std::string& type_ov = "") {
    std::string pw = body.value("password", "");
    if (pw.empty()) return json_error("missing 'password'", 400);
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    std::vector<UNSIGNED8> pwbuf(pw.size() + 1);
    std::memcpy(pwbuf.data(), pw.c_str(), pw.size() + 1);
    AdsSetEncryptionPassword(sess.conn, pwbuf.data());
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED32 rc = AdsEncryptTable(hTable);
    AdsCloseTable(hTable);
    if (rc != 0) return json_error(
        "AdsEncryptTable failed (" + std::to_string(rc) + ")", 500);
    return json{{"ok", true}, {"table", tname}};
}

// studio.web.0.5 — Data Dictionary surface.
//
// Each request opens a fresh DataDict, mutates if needed, calls
// save(), drops it. Stateless web => safe under concurrent edits;
// the DD's own write-then-rename keeps the on-disk file
// crash-consistent.

std::vector<std::string> list_add_files(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (ext == ".add") out.push_back(e.path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

json dd_list(const std::string& dir) {
    auto names = list_add_files(dir);
    json arr = json::array();
    for (auto& n : names) {
        std::error_code ec;
        auto sz = fs::file_size(fs::path(dir) / n, ec);
        arr.push_back(json{
            {"name",  n},
            {"bytes", ec ? 0 : static_cast<std::uint64_t>(sz)}
        });
    }
    return json{{"dicts", arr}, {"data_dir", dir}};
}

json dd_view(const std::string& dir, const std::string& name) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) {
        return json_error("DataDict::open failed: " + p, 404);
    }
    auto& dd = dd_r.value();

    json tables = json::array();
    // DataDict exposes tables_ via has_alias / resolve only; use
    // a private accessor via DBPROP iteration is not available.
    // Workaround: re-parse the file ourselves to enumerate every
    // row. (DataDict doesn't expose a list_tables() API yet.)
    {
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            // crude split on first run of spaces
            std::size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos) continue;
            std::string kind = line.substr(0, sp);
            std::string rest = line.substr(sp);
            // ltrim
            std::size_t a = rest.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            rest = rest.substr(a);
            if (kind == "TABLE") {
                auto eq = rest.find('=');
                if (eq != std::string::npos) {
                    tables.push_back(json{
                        {"alias", rest.substr(0, eq)},
                        {"path",  rest.substr(eq + 1)}
                    });
                }
            }
        }
    }

    json indexes = json::array();
    for (auto& ix : dd.indexes()) {
        indexes.push_back(json{
            {"table",   ix.table_alias},
            {"path",    ix.index_path},
            {"comment", ix.comment}
        });
    }

    json links = json::array();
    for (auto& kv : dd.links()) {
        links.push_back(json{
            {"alias", kv.second.alias},
            {"path",  kv.second.path},
            {"user",  kv.second.user}
        });
    }

    json ri = json::array();
    for (auto& kv : dd.ri()) {
        ri.push_back(json{
            {"name",        kv.second.name},
            {"parent",      kv.second.parent},
            {"child",       kv.second.child},
            {"parent_tag",  kv.second.parent_tag},
            {"child_tag",   kv.second.child_tag},
            {"update_opt",  kv.second.update_opt},
            {"delete_opt",  kv.second.delete_opt},
            {"fail_table",  kv.second.fail_table}
        });
    }

    // Re-parse for users + props (DataDict doesn't expose iteration
    // of users_ or db_props_ directly today).
    json users = json::array();
    json props = json::array();
    {
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::size_t sp = line.find_first_of(" \t");
            if (sp == std::string::npos) continue;
            std::string kind = line.substr(0, sp);
            std::string rest = line.substr(sp);
            std::size_t a = rest.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            rest = rest.substr(a);
            if (kind == "USER") users.push_back(rest);
            else if (kind == "DBPROP") {
                auto eq = rest.find('=');
                if (eq != std::string::npos) {
                    props.push_back(json{
                        {"key",   rest.substr(0, eq)},
                        {"value", rest.substr(eq + 1)}
                    });
                }
            }
        }
    }

    return json{
        {"name",    name},
        {"path",    p},
        {"tables",  tables},
        {"indexes", indexes},
        {"users",   users},
        {"links",   links},
        {"ri",      ri},
        {"db_props", props}
    };
}

json dd_create(const std::string& dir, const std::string& name) {
    fs::path p = fs::path(dir) / name;
    if (fs::exists(p)) {
        return json_error("dictionary already exists: " + name, 409);
    }
    auto r = openads::engine::DataDict::create(p.string());
    if (!r) return json_error("DataDict::create failed", 500);
    return json{{"ok", true}, {"name", name}};
}

json dd_drop(const std::string& dir, const std::string& name) {
    fs::path p = fs::path(dir) / name;
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return json_error("dictionary not found: " + name, 404);
    }
    fs::remove(p, ec);
    return json{{"ok", true}, {"removed", name}};
}

json dd_add_table(const std::string& dir, const std::string& name,
                  const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string alias = body.value("alias", "");
    std::string rel   = body.value("path",  "");
    if (alias.empty() || rel.empty()) {
        return json_error("body needs {alias, path}", 400);
    }
    if (auto r = dd.add_table(alias, rel); !r) {
        return json_error("add_table failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"alias", alias}, {"path", rel}};
}

json dd_remove_table(const std::string& dir, const std::string& name,
                     const std::string& alias) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    if (auto r = dd.remove_table(alias); !r) {
        return json_error("remove_table failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"alias", alias}};
}

json dd_add_user(const std::string& dir, const std::string& name,
                 const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string user = body.value("user", "");
    if (user.empty()) return json_error("body needs {user}", 400);
    if (auto r = dd.create_user(user); !r) {
        return json_error("create_user failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"user", user}};
}

json dd_remove_user(const std::string& dir, const std::string& name,
                    const std::string& user) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    if (auto r = dd.delete_user(user); !r) {
        return json_error("delete_user failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"user", user}};
}

json dd_set_dbprop(const std::string& dir, const std::string& name,
                   const json& body) {
    auto p = (fs::path(dir) / name).string();
    auto dd_r = openads::engine::DataDict::open(p);
    if (!dd_r) return json_error("DataDict::open failed", 404);
    auto& dd = dd_r.value();
    std::string k = body.value("key",   "");
    std::string v = body.value("value", "");
    if (k.empty()) return json_error("body needs {key, value}", 400);
    if (auto r = dd.set_db_property(k, v); !r) {
        return json_error("set_db_property failed", 500);
    }
    if (auto r = dd.save(); !r) return json_error("save failed", 500);
    return json{{"ok", true}, {"key", k}};
}

// studio.web.0.6 — table maintenance ops.
// Each maps to a single Ads* call so the surface stays small.

json table_reindex(const std::string& dir, const std::string& tname,
                   const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED32 rc = AdsReindex(hTable);
    AdsCloseTable(hTable);
    if (rc != 0) return json_error(
        "AdsReindex failed (" + std::to_string(rc) + ")", 500);
    return json{{"ok", true}, {"table", tname}};
}

json table_pack(const std::string& dir, const std::string& tname,
                const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED32 rc = AdsPackTable(hTable);
    AdsCloseTable(hTable);
    if (rc != 0) return json_error(
        "AdsPackTable failed (" + std::to_string(rc) + ")", 500);
    return json{{"ok", true}, {"table", tname}};
}

json table_zap(const std::string& dir, const std::string& tname,
               const std::string& type_ov = "") {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    UNSIGNED8 leaf[256] = {0};
    std::size_t ln = std::min<std::size_t>(tname.size(), sizeof(leaf) - 1);
    std::memcpy(leaf, tname.data(), ln);
    ADSHANDLE hTable = 0;
    if (AdsOpenTable(sess.conn, leaf, nullptr,
                     resolve_table_type(dir, tname, type_ov),
                     0, 0, 0, 0, &hTable) != 0) {
        return json_error("AdsOpenTable failed: " + tname, 404);
    }
    UNSIGNED32 rc = AdsZapTable(hTable);
    AdsCloseTable(hTable);
    if (rc != 0) return json_error(
        "AdsZapTable failed (" + std::to_string(rc) + ")", 500);
    return json{{"ok", true}, {"table", tname}};
}

// studio.web.0.6 — CREATE INDEX through SQL DDL.
//   body: { tag, expr, descending?, unique?, file? }
// `file` is the bag (.cdx by default) the new tag lives in.
json table_create_index(const std::string& dir,
                        const std::string& tname,
                        const json& body) {
    std::string tag  = body.value("tag",  "");
    std::string expr = body.value("expr", "");
    if (tag.empty() || expr.empty()) {
        return json_error("body needs {tag, expr}", 400);
    }
    bool desc   = body.value("descending", false);
    bool unique = body.value("unique",     false);
    std::string sql = "CREATE INDEX " + tag + " ON " + tname +
                      " (" + expr + ")";
    if (desc)   sql += " DESCENDING";
    if (unique) sql += " UNIQUE";

    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir", 500);
    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(sess.conn, &hStmt);
    std::vector<UNSIGNED8> sqlbuf(sql.size() + 1);
    std::memcpy(sqlbuf.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rrc = AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur);
    if (hCur != 0) AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    if (rrc != 0) {
        return json_error("CREATE INDEX failed: " + sql, 400);
    }
    return json{{"ok", true}, {"sql", sql}};
}

// studio.web.0.12 — minimal STORE-only ZIP writer (no compression,
// no encryption). Builds an in-memory archive of every regular
// file under `dir` (one level deep) so admins can download a
// snapshot of the data directory through the browser.
//
// Hand-rolled to avoid pulling in another vendored dependency.
// Works for backup-style admin downloads; not a general-purpose
// archiver.

namespace {

std::uint32_t crc32_calc(const std::uint8_t* data, std::size_t n) {
    static std::uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

struct ZipEntry { std::string name; std::uint32_t local_off, crc, sz; };

std::string build_zip(const std::string& dir) {
    std::vector<std::uint8_t> out;
    std::vector<ZipEntry> entries;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return {};
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        std::string name = e.path().filename().string();
        ZipEntry ze;
        ze.name      = name;
        ze.local_off = static_cast<std::uint32_t>(out.size());
        ze.crc       = crc32_calc(
            reinterpret_cast<const std::uint8_t*>(body.data()),
            body.size());
        ze.sz        = static_cast<std::uint32_t>(body.size());

        // Local file header.
        put_u32(out, 0x04034b50u);          // signature
        put_u16(out, 20);                    // version needed
        put_u16(out, 0);                     // flags
        put_u16(out, 0);                     // method = stored
        put_u16(out, 0);                     // mtime
        put_u16(out, 0);                     // mdate
        put_u32(out, ze.crc);
        put_u32(out, ze.sz);                 // compressed
        put_u32(out, ze.sz);                 // uncompressed
        put_u16(out, static_cast<std::uint16_t>(name.size()));
        put_u16(out, 0);                     // extra
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(),
                   reinterpret_cast<const std::uint8_t*>(body.data()),
                   reinterpret_cast<const std::uint8_t*>(body.data() + body.size()));
        entries.push_back(std::move(ze));
    }

    std::uint32_t cd_off = static_cast<std::uint32_t>(out.size());
    for (auto& ze : entries) {
        put_u32(out, 0x02014b50u);          // central dir signature
        put_u16(out, 20);                    // version made by
        put_u16(out, 20);                    // version needed
        put_u16(out, 0);                     // flags
        put_u16(out, 0);                     // method
        put_u16(out, 0);                     // mtime
        put_u16(out, 0);                     // mdate
        put_u32(out, ze.crc);
        put_u32(out, ze.sz);
        put_u32(out, ze.sz);
        put_u16(out, static_cast<std::uint16_t>(ze.name.size()));
        put_u16(out, 0);                     // extra len
        put_u16(out, 0);                     // comment len
        put_u16(out, 0);                     // disk number
        put_u16(out, 0);                     // internal attrs
        put_u32(out, 0);                     // external attrs
        put_u32(out, ze.local_off);
        out.insert(out.end(), ze.name.begin(), ze.name.end());
    }
    std::uint32_t cd_size = static_cast<std::uint32_t>(out.size()) - cd_off;

    // End of central dir.
    put_u32(out, 0x06054b50u);
    put_u16(out, 0);                         // disk
    put_u16(out, 0);                         // disk with CD
    put_u16(out, static_cast<std::uint16_t>(entries.size()));
    put_u16(out, static_cast<std::uint16_t>(entries.size()));
    put_u32(out, cd_size);
    put_u32(out, cd_off);
    put_u16(out, 0);                         // comment len

    return std::string(reinterpret_cast<const char*>(out.data()),
                       out.size());
}

} // namespace

// studio.web.0.7 — companion files for a single DBF (.cdx / .ntx /
// .fpt / .dbt / .dbv) with sizes. Useful for the Structure tab so
// admins can see at a glance which sidecars exist + how big they
// are without opening every index bag.
json table_sidecars(const std::string& dir, const std::string& tname) {
    fs::path base = fs::path(dir) / tname;
    fs::path stem = base.parent_path() / base.stem();
    json arr = json::array();
    static const std::vector<std::string> exts =
        {".cdx", ".ntx", ".fpt", ".dbt", ".dbv"};
    std::error_code ec;
    for (auto& ext : exts) {
        fs::path p = stem;
        p += ext;
        if (!fs::exists(p, ec)) continue;
        auto sz = fs::file_size(p, ec);
        arr.push_back(json{
            {"file",  p.filename().string()},
            {"kind",  ext.substr(1)},
            {"bytes", ec ? 0 : static_cast<std::uint64_t>(sz)}
        });
    }
    return json{{"table", tname}, {"sidecars", arr}};
}

// studio.web.0.14 — host OS / arch / compiler banner so Studio can
// surface "running on Linux x86_64 / clang 18" etc. Detected at
// compile time so a packaged binary always reports the platform
// it was built for.
const char* host_os() {
    // Only report platforms that are actually tested in CI and on
    // the project's reference servers: Windows / Linux / macOS.
    // Anything else compiles, but we report it as "untested" so
    // users know they're running on an unsupported configuration.
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "untested";
#endif
}
const char* host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}
const char* host_compiler() {
#if defined(__clang__)
    static char b[40];
    std::snprintf(b, sizeof(b), "clang %d.%d.%d",
                  __clang_major__, __clang_minor__, __clang_patchlevel__);
    return b;
#elif defined(__GNUC__)
    static char b[40];
    std::snprintf(b, sizeof(b), "gcc %d.%d.%d",
                  __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    return b;
#elif defined(_MSC_VER)
    static char b[40];
    std::snprintf(b, sizeof(b), "MSVC %d", _MSC_VER);
    return b;
#else
    return "unknown";
#endif
}

// studio.web.0.2 — server info panel.
json server_info(const std::string& dir) {
    json out{
        {"engine",      "openads"},
        {"version",     OPENADS_VERSION_STR},
        {"data_dir",    dir},
        {"http",        "studio.web.0.14"},
        {"os",          host_os()},
        {"arch",        host_arch()},
        {"compiler",    host_compiler()}
    };
    AbiSession sess(dir);
    if (sess.ok()) {
        UNSIGNED8 buf[256] = {0};
        UNSIGNED16 cap = sizeof(buf);
        if (AdsGetServerName(sess.conn, buf, &cap) == 0) {
            out["server_name"] = std::string(reinterpret_cast<char*>(buf), cap);
        }
    }
    auto tables = list_dbf_files(dir);
    out["tables"] = tables;

    // studio.web.0.7 — aggregate stats (total tables, dbf bytes,
    // sidecar bytes) so the Server tab can surface a one-glance
    // health summary.
    std::error_code ec;
    std::uint64_t dbf_bytes = 0, sidecar_bytes = 0;
    for (auto& t : tables) {
        auto p = fs::path(dir) / t;
        if (auto sz = fs::file_size(p, ec); !ec) dbf_bytes += sz;
        // sidecars by stem
        auto stem = p.parent_path() / p.stem();
        for (auto& ext : {".cdx", ".ntx", ".fpt", ".dbt", ".dbv"}) {
            fs::path sp = stem; sp += ext;
            if (auto sz = fs::file_size(sp, ec); !ec) sidecar_bytes += sz;
        }
    }
    out["dbf_bytes"]      = dbf_bytes;
    out["sidecar_bytes"]  = sidecar_bytes;
    out["total_bytes"]    = dbf_bytes + sidecar_bytes;
    out["dict_count"]     = list_add_files(dir).size();
    return out;
}

// Run a SQL string + materialise up to `limit` rows as a row-major
// table { cols, rows }.
json run_sql(const std::string& dir, const std::string& sql,
             std::uint32_t limit) {
    AbiSession sess(dir);
    if (!sess.ok()) return json_error("could not open data dir: " + dir, 500);

    ADSHANDLE hStmt = 0;
    AdsCreateSQLStatement(sess.conn, &hStmt);
    std::vector<UNSIGNED8> sqlbuf(sql.size() + 1);
    std::memcpy(sqlbuf.data(), sql.c_str(), sql.size() + 1);
    ADSHANDLE hCur = 0;
    UNSIGNED32 rrc = AdsExecuteSQLDirect(hStmt, sqlbuf.data(), &hCur);
    if (rrc != 0) {
        // Capture the engine error BEFORE AdsCloseSQLStatement —
        // ok() inside the close clears thread-local last_error.
        char emsg[512] = {0};
        UNSIGNED16 elen = sizeof(emsg);
        UNSIGNED32 ecode = 0;
        AdsGetLastError(&ecode,
                        reinterpret_cast<UNSIGNED8*>(emsg), &elen);
        AdsCloseSQLStatement(hStmt);
        std::string detail(emsg, elen);
        // Soft heuristics — surface the most common SQL-101 traps
        // back to the user as a "did you mean…?" hint.
        std::string hint;
        if (sql.find('"') != std::string::npos) {
            hint = " hint: ADS SQL uses single quotes for string "
                   "literals — try '...' instead of \"...\".";
        }
        return json_error(
            "AdsExecuteSQLDirect failed (" +
            std::to_string(rrc) + ")" +
            (detail.empty() ? "" : ": " + detail) + hint,
            400);
    }
    json out{{"cols", json::array()}, {"rows", json::array()},
             {"rows_returned", 0}};
    if (hCur == 0) {
        AdsCloseSQLStatement(hStmt);
        return out;
    }
    UNSIGNED16 nfields = 0;
    AdsGetNumFields(hCur, &nfields);
    std::vector<std::string> col_names;
    col_names.reserve(nfields);
    for (UNSIGNED16 i = 1; i <= nfields; ++i) {
        UNSIGNED8 nm[128] = {0};
        UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(hCur, i, nm, &cap);
        col_names.emplace_back(reinterpret_cast<char*>(nm), cap);
        out["cols"].push_back(col_names.back());
    }
    AdsGotoTop(hCur);
    UNSIGNED16 atend = 0;
    AdsAtEOF(hCur, &atend);
    std::uint32_t walked = 0;
    while (atend == 0 && walked < limit) {
        json row = json::array();
        for (auto& cn : col_names) {
            UNSIGNED8 fbuf[64] = {0};
            std::size_t n = std::min<std::size_t>(cn.size(), sizeof(fbuf) - 1);
            std::memcpy(fbuf, cn.data(), n);
            UNSIGNED8 vbuf[4096] = {0};
            UNSIGNED32 vcap = sizeof(vbuf);
            UNSIGNED32 fr = AdsGetField(hCur, fbuf, vbuf, &vcap, 0);
            if (fr != 0) vcap = 0;
            // Trim trailing spaces (DBF blank-pads fixed-width).
            while (vcap > 0 && vbuf[vcap - 1] == ' ') --vcap;
            row.push_back(std::string(reinterpret_cast<char*>(vbuf), vcap));
        }
        out["rows"].push_back(std::move(row));
        ++walked;
        AdsSkip(hCur, 1);
        AdsAtEOF(hCur, &atend);
    }
    out["rows_returned"] = walked;
    AdsCloseTable(hCur);
    AdsCloseSQLStatement(hStmt);
    return out;
}

} // namespace

HttpConsole::HttpConsole() : srv_(std::make_unique<httplib::Server>()) {}
HttpConsole::~HttpConsole() { stop(); }

void HttpConsole::add_user(const std::string& user,
                            const std::string& password) {
    users_[user] = password;
}

namespace {

// Decode the body of an `Authorization: Basic <base64>` header.
// Returns "" on any malformed input.
std::string b64decode(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    int dec[256];
    for (int i = 0; i < 256; ++i) dec[i] = -1;
    for (int i = 0; i < 64; ++i)
        dec[static_cast<unsigned char>(tbl[i])] = i;
    std::string out;
    int v = 0, b = 0;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int d = dec[c];
        if (d < 0) return "";
        v = (v << 6) | d;
        b += 6;
        if (b >= 8) {
            b -= 8;
            out.push_back(static_cast<char>((v >> b) & 0xFF));
        }
    }
    return out;
}

// "user:pass" (already decoded) against the registered credential map.
bool user_pass_valid(const std::string& raw,
                      const std::unordered_map<std::string, std::string>& users) {
    auto colon = raw.find(':');
    if (colon == std::string::npos) return false;
    std::string user = raw.substr(0, colon);
    std::string pw   = raw.substr(colon + 1);
    auto cit = users.find(user);
    return cit != users.end() && cit->second == pw;
}

bool check_basic_auth(const httplib::Request& req,
                      const std::unordered_map<std::string, std::string>& users) {
    if (users.empty()) return true;       // dev mode: no auth
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return false;
    const std::string& v = it->second;
    const std::string prefix = "Basic ";
    if (v.size() < prefix.size() ||
        v.compare(0, prefix.size(), prefix) != 0) return false;
    return user_pass_valid(b64decode(v.substr(prefix.size())), users);
}

// Cookie-session auth. Embedding credentials straight in the
// navigated-to URL (`http://user:pass@host/...`) is a real trap for
// any SPA served from the same origin: once `document.location`
// carries userinfo, the Fetch spec refuses to build a Request from
// any URL resolved against it ("Request cannot be constructed from a
// URL that includes credentials"), so every `fetch("/api/...")` the
// page's own JS makes throws. Cookies don't have that restriction and
// ride along on every same-origin request (page loads + fetch) with
// no JS changes needed. The cookie value is the same "user:pass"
// base64 blob Basic-auth already uses, so verification reuses
// user_pass_valid() as-is.
const char* kAuthCookieName = "oads_auth";

bool check_cookie_auth(const httplib::Request& req,
                       const std::unordered_map<std::string, std::string>& users) {
    if (users.empty()) return true;
    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return false;
    const std::string& all = it->second;
    const std::string needle = std::string(kAuthCookieName) + "=";
    std::size_t pos = 0;
    while (pos < all.size()) {
        std::size_t semi = all.find(';', pos);
        std::string part = all.substr(pos, semi == std::string::npos ?
                                       std::string::npos : semi - pos);
        std::size_t b = part.find_first_not_of(' ');
        if (b != std::string::npos) part = part.substr(b);
        if (part.compare(0, needle.size(), needle) == 0 &&
            user_pass_valid(b64decode(part.substr(needle.size())), users)) {
            return true;
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return false;
}

} // namespace

bool HttpConsole::start(const std::string& host,
                         std::uint16_t      port,
                         const std::string& data_dir,
                         openads::network::Server* wire_srv) {
    data_dir_ = data_dir;
    wire_srv_ = wire_srv;
    auto& srv = *srv_;

    // studio.web.0.8 — auth gate. Empty credential map == dev mode (no
    // auth, unchanged). Otherwise a request passes with either a
    // valid `Authorization: Basic …` header OR the oads_auth session
    // cookie (see check_cookie_auth above for why the cookie exists —
    // embedding credentials in the page URL breaks the SPA's own
    // fetch() calls).
    //
    // GET /?_auth_user=U&_auth_pass=P is the one-time login path: a
    // host embedding the console navigates here once with its
    // generated credentials in the query string (NOT userinfo, so
    // this specific request is unaffected by the Fetch restriction
    // above — it's a top-level navigation, not a fetch()). On a match
    // we set the session cookie and 302-redirect to the same view
    // without the credentials, so they never end up in
    // document.location for the SPA's own JS to trip over.
    srv.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            if (req.path == "/" && req.has_param("_auth_user") &&
                req.has_param("_auth_pass")) {
                std::string user = req.get_param_value("_auth_user");
                std::string pass = req.get_param_value("_auth_pass");
                if (users_.empty() || !user_pass_valid(user + ":" + pass, users_)) {
                    // Audit trail: every failed login attempt lands in
                    // the same ads_err.dbf the rest of the engine
                    // already uses (sp_mgGetErrorLog reads it), so a
                    // rejected attempt is never silent.
                    openads::mgmt::ErrorLog::instance().log(
                        401, "STUDIO_AUTH", 0,
                        "login failed from " + req.remote_addr +
                        " user=" + user);
                    res.status = 403;
                    res.set_content("invalid credentials", "text/plain; charset=utf-8");
                    return httplib::Server::HandlerResponse::Handled;
                }
                openads::mgmt::ErrorLog::instance().log(
                    0, "STUDIO_AUTH", 0,
                    "login ok from " + req.remote_addr + " user=" + user);
                auto blob = base64_encode(
                    reinterpret_cast<const std::uint8_t*>((user + ":" + pass).data()),
                    user.size() + 1 + pass.size());
                res.set_header("Set-Cookie",
                    std::string(kAuthCookieName) + "=" + blob +
                    "; HttpOnly; SameSite=Strict; Path=/");
                std::string loc = "/?";
                if (req.has_param("table"))
                    loc += "table=" + httplib::detail::encode_url(req.get_param_value("table")) + "&";
                if (req.has_param("tab"))
                    loc += "tab=" + httplib::detail::encode_url(req.get_param_value("tab")) + "&";
                if (req.has_param("host"))
                    loc += "host=" + httplib::detail::encode_url(req.get_param_value("host"));
                res.status = 302;
                res.set_header("Location", loc);
                return httplib::Server::HandlerResponse::Handled;
            }
            if (check_basic_auth(req, users_) || check_cookie_auth(req, users_)) {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            // Every other request only reaches here with users_ non-empty
            // (see check_basic_auth/check_cookie_auth's own dev-mode
            // early-return) -- so this IS always a genuine rejection,
            // never a false positive from the dev-mode/no-auth path.
            openads::mgmt::ErrorLog::instance().log(
                401, "STUDIO_AUTH", 0,
                "unauthenticated request from " + req.remote_addr +
                " " + req.method + " " + req.path);
            res.status = 401;
            res.set_header("WWW-Authenticate",
                           "Basic realm=\"OpenADS Studio\"");
            res.set_content("authentication required",
                            "text/plain; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        });

    srv.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kSpaIndexHtml, "text/html; charset=utf-8");
    });

    srv.Get("/api/health", [this](const httplib::Request&,
                                   httplib::Response& res) {
        // studio.web.0.17 — expose deployment mode so the SPA can
        // render a "LocalServer" vs "Remote Server" badge in the
        // header. The presence of a backing wire-server pointer
        // (wire_srv_) is the canonical signal: openads_serverd
        // hands one in, the in-process ace64.dll path passes
        // nullptr.
        const char* mode =
            (wire_srv_ != nullptr) ? "remote-server" : "localserver";
        json j{{"status", "ok"},
               {"engine", "openads"},
               {"mode",   mode},
               {"data_dir", data_dir_}};
        res.set_content(j.dump(), "application/json");
    });

    srv.Get("/api/tables",
            [this](const httplib::Request&, httplib::Response& res) {
        auto tables = list_dbf_files(data_dir_);
        json j{{"data_dir", data_dir_},
               {"tables",   tables}};
        res.set_content(j.dump(), "application/json");
    });

    auto type_param = [](const httplib::Request& req) -> std::string {
        return req.has_param("type") ? req.get_param_value("type") : "";
    };

    srv.Get(R"(/api/tables/(.+)/schema)",
            [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json j = table_schema(data_dir_, req.matches[1], type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get(R"(/api/tables/(.+)/rows)",
            [this, type_param](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t offset = 0, limit = 50;
        if (req.has_param("offset"))
            offset = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("offset").c_str(), nullptr, 10));
        if (req.has_param("limit"))
            limit  = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("limit").c_str(), nullptr, 10));
        if (limit == 0 || limit > 5000) limit = 50;
        std::string aof_cond;
        if (req.has_param("aof")) aof_cond = req.get_param_value("aof");
        json j = table_rows(data_dir_, req.matches[1], offset, limit,
                            type_param(req), aof_cond);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/(.+)/insert)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_insert(data_dir_, req.matches[1], body,
                              type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/(.+)/update)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t recno = 0;
        if (req.has_param("recno"))
            recno = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("recno").c_str(), nullptr, 10));
        if (recno == 0) { res.status = 400;
            res.set_content(json_error("missing recno query param", 400).dump(),
                            "application/json"); return; }
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_update(data_dir_, req.matches[1], recno, body,
                              type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/(.+)/delete)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        std::uint32_t recno = 0;
        if (req.has_param("recno"))
            recno = static_cast<std::uint32_t>(
                std::strtoul(req.get_param_value("recno").c_str(), nullptr, 10));
        bool recall = req.has_param("recall") &&
                      req.get_param_value("recall") == "1";
        if (recno == 0) { res.status = 400;
            res.set_content(json_error("missing recno query param", 400).dump(),
                            "application/json"); return; }
        json j = table_delete(data_dir_, req.matches[1], recno, recall,
                              type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get("/api/server/info",
            [this](const httplib::Request&, httplib::Response& res) {
        json j = server_info(data_dir_);
        res.set_content(j.dump(), "application/json");
    });

    // studio.web.0.12 — backup data dir as a STORE-only ZIP.
    srv.Get("/api/server/backup",
            [this](const httplib::Request&, httplib::Response& res) {
        auto body = build_zip(data_dir_);
        if (body.empty()) {
            res.status = 500;
            res.set_content(json_error("backup failed", 500).dump(),
                            "application/json");
            return;
        }
        char ts[32]; std::time_t t = std::time(nullptr);
        struct tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_buf);
        std::string fname = std::string("openads-backup-") + ts + ".zip";
        res.set_header("Content-Disposition",
                       "attachment; filename=\"" + fname + "\"");
        res.set_content(body, "application/zip");
    });

    // studio.web.0.5 — Data Dictionary endpoints.
    srv.Get("/api/dd",
            [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(dd_list(data_dir_).dump(), "application/json");
    });
    srv.Post("/api/dd",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        std::string n = body.value("name", "");
        if (n.empty()) { res.status = 400;
            res.set_content(json_error("body needs {name}", 400).dump(),
                            "application/json"); return; }
        json j = dd_create(data_dir_, n);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Get(R"(/api/dd/([^/]+))",
            [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_view(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_drop(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/tables)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_add_table(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+)/tables/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_remove_table(data_dir_, req.matches[1], req.matches[2]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/users)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_add_user(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Delete(R"(/api/dd/([^/]+)/users/([^/]+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = dd_remove_user(data_dir_, req.matches[1], req.matches[2]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/dd/([^/]+)/dbprop)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = dd_set_dbprop(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/server/sessions/(\d+)/kill)",
             [this](const httplib::Request& req, httplib::Response& res) {
        if (wire_srv_ == nullptr) {
            res.status = 503;
            res.set_content(json_error("wire server unavailable", 503).dump(),
                            "application/json");
            return;
        }
        std::uint64_t id = static_cast<std::uint64_t>(
            std::strtoull(req.matches[1].str().c_str(), nullptr, 10));
        bool ok = wire_srv_->kill_session(id);
        if (!ok) {
            res.status = 404;
            res.set_content(json_error("unknown session id", 404).dump(),
                            "application/json");
            return;
        }
        res.set_content(json{{"ok", true}, {"id", id}}.dump(),
                        "application/json");
    });

    srv.Get("/api/server/sessions",
            [this](const httplib::Request&, httplib::Response& res) {
        if (wire_srv_ == nullptr) {
            res.set_content(json{{"sessions", json::array()},
                                 {"hint", "wire server unavailable"}}
                            .dump(), "application/json");
            return;
        }
        auto now = std::chrono::system_clock::now();
        json arr = json::array();
        for (auto& s : wire_srv_->sessions_snapshot()) {
            auto secs = [&](auto t){
                return std::chrono::duration_cast<std::chrono::seconds>(
                    now - t).count();
            };
            arr.push_back(json{
                {"id",            s.id},
                {"peer_ip",       s.peer_ip},
                {"peer_port",     s.peer_port},
                {"user",          s.user},
                {"data_dir",      s.data_dir},
                {"connected_secs", static_cast<long long>(secs(s.connected_at))},
                {"idle_secs",      static_cast<long long>(secs(s.last_activity))},
                {"frames_in",     s.frames_in},
                {"frames_out",    s.frames_out},
                {"open_tables",   s.open_tables}
            });
        }
        res.set_content(json{{"sessions", arr},
                             {"count", arr.size()}}.dump(),
                        "application/json");
    });

    srv.Post("/api/tables",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_create(data_dir_, body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Delete(R"(/api/tables/(.+))",
               [this](const httplib::Request& req, httplib::Response& res) {
        json j = table_drop(data_dir_, req.matches[1]);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Post(R"(/api/tables/(.+)/encrypt)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_encrypt(data_dir_, req.matches[1], body,
                               type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    // studio.web.0.6 — table maintenance (Reindex / Pack / Zap +
    // CREATE INDEX wizard).
    srv.Post(R"(/api/tables/(.+)/reindex)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json j = table_reindex(data_dir_, req.matches[1], type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/tables/(.+)/pack)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json j = table_pack(data_dir_, req.matches[1], type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/tables/(.+)/zap)",
             [this, type_param](const httplib::Request& req, httplib::Response& res) {
        json j = table_zap(data_dir_, req.matches[1], type_param(req));
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });
    srv.Post(R"(/api/tables/(.+)/index)",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body; try { body = json::parse(req.body); }
        catch (...) { res.status = 400;
            res.set_content(json_error("invalid JSON", 400).dump(),
                            "application/json"); return; }
        json j = table_create_index(data_dir_, req.matches[1], body);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    srv.Get(R"(/api/tables/(.+)/sidecars)",
            [this](const httplib::Request& req, httplib::Response& res) {
        json j = table_sidecars(data_dir_, req.matches[1]);
        res.set_content(j.dump(), "application/json");
    });

    // studio.web.0.8 — download a single DBF (or any sidecar) as
    // application/octet-stream so the admin can pull a copy off the
    // server through the browser.
    srv.Get(R"(/api/tables/(.+)/download)",
            [this](const httplib::Request& req, httplib::Response& res) {
        std::string fname = req.matches[1];
        if (fname.find("..") != std::string::npos ||
            fname.find('/')  != std::string::npos ||
            fname.find('\\') != std::string::npos) {
            res.status = 400;
            res.set_content("invalid file name", "text/plain");
            return;
        }
        fs::path p = fs::path(data_dir_) / fname;
        std::error_code ec;
        if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) {
            res.status = 404;
            res.set_content("not found", "text/plain");
            return;
        }
        std::ifstream in(p, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
        res.set_header("Content-Disposition",
            "attachment; filename=\"" + fname + "\"");
        res.set_content(body, "application/octet-stream");
    });

    // studio.web.0.7 — DBF + sidecar upload from the browser.
    // multipart/form-data: one or more file fields named "file"; each
    // file is written to the data dir under its uploaded basename
    // (path traversal is rejected).
    srv.Post("/api/upload",
             [this](const httplib::Request& req, httplib::Response& res) {
        json arr = json::array();
        for (auto& kv : req.files) {
            const auto& f = kv.second;
            std::string fname = f.filename;
            if (fname.empty()) continue;
            // Reject path traversal.
            if (fname.find("..") != std::string::npos ||
                fname.find('/')  != std::string::npos ||
                fname.find('\\') != std::string::npos) {
                continue;
            }
            fs::path dest = fs::path(data_dir_) / fname;
            std::ofstream out(dest, std::ios::binary);
            out.write(f.content.data(),
                      static_cast<std::streamsize>(f.content.size()));
            arr.push_back(json{{"file", fname},
                               {"bytes", f.content.size()}});
        }
        res.set_content(json{{"ok", true},
                             {"saved", arr},
                             {"count", arr.size()}}.dump(),
                        "application/json");
    });

    srv.Post("/api/sql",
             [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(json_error("invalid JSON body", 400).dump(),
                            "application/json");
            return;
        }
        std::string sql = body.value("sql", "");
        std::uint32_t limit = body.value("limit", 200u);
        if (sql.empty()) {
            res.status = 400;
            res.set_content(json_error("missing 'sql' field", 400).dump(),
                            "application/json");
            return;
        }
        json j = run_sql(data_dir_, sql, limit);
        if (j.contains("error")) res.status = j.value("http_code", 500);
        res.set_content(j.dump(), "application/json");
    });

    if (!srv.bind_to_port(host, port)) return false;
    running_.store(true);
    thread_ = std::thread([this]() {
        srv_->listen_after_bind();
        running_.store(false);
    });
    return true;
}

void HttpConsole::stop() {
    if (!running_.load() && !thread_.joinable()) return;
    if (srv_) srv_->stop();
    if (thread_.joinable()) thread_.join();
    running_.store(false);
}

} // namespace openads::studio

#endif // OPENADS_WITH_HTTP
