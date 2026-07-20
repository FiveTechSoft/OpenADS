#include "engine/data_dict.h"

#include "platform/file.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

namespace openads::engine {

// ---------------------------------------------------------------------------
// OpenADS DD ADT-format constants
// ---------------------------------------------------------------------------
//
// .add  = ADT-compatible table (ADT signature header + 6 field descriptors
//         + fixed-length records).
// .am   = ADM-compatible memo file (256-byte blocks, next_avail at header+20).
//
// Field layout in the record (342 bytes total):
//   [0]        del flag: 0x04=active, 0x05=deleted
//   [1..4]     null bitmap (4 bytes, always zero)
//   [5..8]     OBJ_ID    (uint32 LE, ADT Integer type 11, length 4)
//   [9..12]    PARENT_ID (uint32 LE, ADT Integer type 11, length 4)
//   [13..32]   OBJ_TYPE  (CHAR 20, space-padded,  ADT type 4)
//   [33..232]  OBJ_NAME  (CHAR 200, space-padded, ADT type 4)
//   [233..332] OBJ_KEY   (CHAR 100, space-padded, ADT type 4)
//   [333..341] OBJ_DATA  (Memo 9:  uint32 block_no LE + uint32 data_len LE + 0x00)

static constexpr std::uint32_t kDdHdrBase = 400;
static constexpr std::uint32_t kDdFldSize = 200;
static constexpr std::uint32_t kDdNumFlds = 6;
static constexpr std::uint32_t kDdHdrLen  = kDdHdrBase + kDdNumFlds * kDdFldSize; // 1600
static constexpr std::uint32_t kDdRecLen  = 342;
static constexpr std::uint32_t kAmBlock   = 256;

// ---------------------------------------------------------------------------
// Low-level integer I/O
// ---------------------------------------------------------------------------

namespace {

static inline std::uint32_t g32(const std::uint8_t* p, std::uint32_t off = 0) {
    return  static_cast<std::uint32_t>(p[off])
          | (static_cast<std::uint32_t>(p[off+1]) <<  8)
          | (static_cast<std::uint32_t>(p[off+2]) << 16)
          | (static_cast<std::uint32_t>(p[off+3]) << 24);
}

static inline void p32(std::uint8_t* p, std::uint32_t off, std::uint32_t v) {
    p[off+0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[off+1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[off+2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[off+3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

static inline void p16(std::uint8_t* p, std::uint32_t off, std::uint16_t v) {
    p[off+0] = static_cast<std::uint8_t>( v       & 0xFFu);
    p[off+1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

// Trim trailing spaces from a fixed-length CHAR field.
static std::string chf(const std::uint8_t* p, std::uint32_t len) {
    std::size_t n = len;
    while (n > 0 && p[n-1] == ' ') --n;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// Derive .am path from .add path (replace extension).
static std::string am_path(const std::string& add_path) {
    auto dot = add_path.rfind('.');
    auto sep = add_path.find_last_of("/\\");
    if (dot == std::string::npos ||
        (sep != std::string::npos && dot < sep))
        return add_path + ".am";
    return add_path.substr(0, dot) + ".am";
}


// ---------------------------------------------------------------------------
// Binary .add helpers — string-based LE readers and record struct
// ---------------------------------------------------------------------------

static inline std::uint32_t le32(const std::string& buf, std::size_t off) {
    return  static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[off]))
          | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[off+1])) <<  8)
          | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[off+2])) << 16)
          | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(buf[off+3])) << 24);
}

static inline std::uint16_t le16(const std::string& buf, std::size_t off) {
    return static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(static_cast<std::uint8_t>(buf[off]))
            | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(buf[off+1])) << 8));
}

// Split a string on embedded NUL bytes.
static std::vector<std::string> split_nul(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\0') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Read a JSON blob from the companion .am buffer using a 9-byte
// more_property pointer ([4-byte LE block] [4-byte LE len] [0x00]).
// Block size for the SAP continuation .am is 8 bytes.
static std::string read_am_json(const std::string& am_buf,
                                const std::array<std::uint8_t, 9>& mp) {
    std::uint32_t blk = static_cast<std::uint32_t>(mp[0])
                      | (static_cast<std::uint32_t>(mp[1]) <<  8)
                      | (static_cast<std::uint32_t>(mp[2]) << 16)
                      | (static_cast<std::uint32_t>(mp[3]) << 24);
    std::uint32_t len = static_cast<std::uint32_t>(mp[4])
                      | (static_cast<std::uint32_t>(mp[5]) <<  8)
                      | (static_cast<std::uint32_t>(mp[6]) << 16)
                      | (static_cast<std::uint32_t>(mp[7]) << 24);
    if (blk == 0 || len == 0 || am_buf.empty()) return {};
    std::size_t off = static_cast<std::size_t>(blk) * 8;
    if (off >= am_buf.size()) return {};
    std::size_t readable = std::min<std::size_t>(len, am_buf.size() - off);
    return am_buf.substr(off, readable);
}

// ---------------------------------------------------------------------------
// JSON helpers (shared by trigger/proc/func/view serialization)
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 4);
    for (char raw : s) {
        auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\r': r += "\\r";  break;
            case '\t': r += "\\t";  break;
            default:
                if (c < 0x20u) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(c));
                    r += buf;
                } else {
                    r += static_cast<char>(c);
                }
                break;
        }
    }
    return r;
}

static std::string trigger_to_json(const DataDict::TriggerEntry& e) {
    std::string j;
    j.reserve(e.container.size() + 200);
    j += "{\"fmt\":1,\"type\":\"Trigger\"";
    j += ",\"name\":\"";      j += json_escape(e.name);        j += '"';
    j += ",\"table\":\"";     j += json_escape(e.table_alias); j += '"';
    j += ",\"event\":";       j += std::to_string(e.event_mask);
    j += ",\"timing\":";      j += std::to_string(e.timing);
    j += ",\"priority\":";    j += std::to_string(e.priority);
    j += std::string(",\"enabled\":") + (e.enabled ? "true" : "false");
    j += ",\"container\":\""; j += json_escape(e.container);   j += '"';
    j += ",\"procedure\":\""; j += json_escape(e.procedure);   j += '"';
    j += ",\"comment\":\"";   j += json_escape(e.comment);     j += '"';
    j += ",\"options\":";     j += std::to_string(e.options);
    j += '}';
    return j;
}

static std::unordered_map<std::string,std::string> json_parse_flat(const std::string& s) {
    std::unordered_map<std::string,std::string> m;
    std::size_t i = 0, n = s.size();
    auto skip_ws = [&]() {
        while (i < n && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i;
    };
    auto read_str = [&]() -> std::string {
        if (i >= n || s[i] != '"') return {};
        ++i;
        std::string r;
        while (i < n && s[i] != '"') {
            if (s[i] == '\\' && i+1 < n) {
                ++i;
                switch (s[i]) {
                    case '"':  r += '"';  break;
                    case '\\': r += '\\'; break;
                    case 'n':  r += '\n'; break;
                    case 'r':  r += '\r'; break;
                    case 't':  r += '\t'; break;
                    case 'u':
                        // Decode \uXXXX (BMP only; handles what json_escape emits).
                        if (i + 4 < n) {
                            unsigned cp = 0;
                            bool ok = true;
                            for (int k = 1; k <= 4; ++k) {
                                char h = s[i + static_cast<std::size_t>(k)];
                                unsigned nib = 0;
                                if      (h >= '0' && h <= '9') nib = static_cast<unsigned>(h - '0');
                                else if (h >= 'a' && h <= 'f') nib = static_cast<unsigned>(h - 'a') + 10u;
                                else if (h >= 'A' && h <= 'F') nib = static_cast<unsigned>(h - 'A') + 10u;
                                else { ok = false; break; }
                                cp = (cp << 4) | nib;
                            }
                            if (ok) {
                                i += 4;
                                // Emit as UTF-8 (values 0x0000..0xFFFF).
                                if (cp == 0) {
                                    // Silently drop embedded NUL bytes; they were
                                    // SAP trailing terminators that leaked into the
                                    // stored string.  This repairs old imports.
                                } else if (cp < 0x80u) {
                                    r += static_cast<char>(cp);
                                } else if (cp < 0x800u) {
                                    r += static_cast<char>(0xC0u | (cp >> 6));
                                    r += static_cast<char>(0x80u | (cp & 0x3Fu));
                                } else {
                                    r += static_cast<char>(0xE0u | (cp >> 12));
                                    r += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                                    r += static_cast<char>(0x80u | (cp & 0x3Fu));
                                }
                                break;
                            }
                        }
                        r += 'u';  // fallback: emit literal 'u'
                        break;
                    default:   r += s[i]; break;
                }
            } else { r += s[i]; }
            ++i;
        }
        if (i < n) ++i;
        return r;
    };
    skip_ws();
    if (i >= n || s[i] != '{') return m;
    ++i;
    while (i < n) {
        skip_ws();
        if (i >= n || s[i] == '}') break;
        if (s[i] == ',') { ++i; continue; }
        if (s[i] != '"') break;
        std::string key = read_str();
        skip_ws();
        if (i >= n || s[i] != ':') break;
        ++i; skip_ws();
        std::string val;
        if (i < n && s[i] == '"') {
            val = read_str();
        } else {
            std::size_t st = i;
            while (i < n && s[i] != ',' && s[i] != '}') ++i;
            val = s.substr(st, i - st);
            while (!val.empty() && (val.back()==' '||val.back()=='\t'))
                val.pop_back();
        }
        m[key] = std::move(val);
    }
    return m;
}

static bool json_to_trigger(const std::string& json,
                             DataDict::TriggerEntry& e) {
    auto m = json_parse_flat(json);
    if (m.count("type") && m.at("type") != "Trigger") return false;
    if (m.count("name"))      e.name        = m.at("name");
    if (m.count("table"))     e.table_alias = m.at("table");
    if (m.count("event"))     try { e.event_mask = static_cast<std::uint32_t>(std::stoul(m.at("event")));   } catch (...) {}
    if (m.count("timing"))    try { e.timing     = static_cast<std::uint32_t>(std::stoul(m.at("timing")));  } catch (...) {}
    if (m.count("priority"))  try { e.priority   = static_cast<std::uint32_t>(std::stoi(m.at("priority"))); } catch (...) {}
    if (m.count("enabled"))   e.enabled   = (m.at("enabled") == "true");
    if (m.count("container")) e.container = m.at("container");
    if (m.count("procedure")) e.procedure = m.at("procedure");
    if (m.count("comment"))   e.comment   = m.at("comment");
    if (m.count("options"))   try { e.options = static_cast<std::uint32_t>(std::stoul(m.at("options"))); } catch (...) {}
    return true;
}

static std::string proc_to_json(const DataDict::ProcEntry& e) {
    std::string j;
    j.reserve(e.procedure.size() + 200);
    j += "{\"fmt\":1,\"type\":\"Procedure\"";
    j += ",\"name\":\"";      j += json_escape(e.name);          j += '"';
    j += ",\"container\":\""; j += json_escape(e.container);     j += '"';
    j += ",\"body\":\"";      j += json_escape(e.procedure);     j += '"';
    j += ",\"input\":\"";     j += json_escape(e.input_params);  j += '"';
    j += ",\"output\":\"";    j += json_escape(e.output_params); j += '"';
    j += ",\"comment\":\"";   j += json_escape(e.comment);       j += '"';
    j += '}';
    return j;
}

static bool json_to_proc(const std::string& json, DataDict::ProcEntry& e) {
    auto m = json_parse_flat(json);
    if (m.count("type") && m.at("type") != "Procedure") return false;
    if (m.count("name"))      e.name          = m.at("name");
    if (m.count("container")) e.container     = m.at("container");
    if (m.count("body"))      e.procedure     = m.at("body");
    if (m.count("input"))     e.input_params  = m.at("input");
    if (m.count("output"))    e.output_params = m.at("output");
    if (m.count("comment"))   e.comment       = m.at("comment");
    return true;
}

static std::string func_to_json(const DataDict::FunctionEntry& e) {
    std::string j;
    j.reserve(e.implementation.size() + 200);
    j += "{\"fmt\":1,\"type\":\"Function\"";
    j += ",\"name\":\"";      j += json_escape(e.name);           j += '"';
    j += ",\"container\":\""; j += json_escape(e.container);      j += '"';
    j += ",\"return\":\"";    j += json_escape(e.return_type);    j += '"';
    j += ",\"input\":\"";     j += json_escape(e.input_params);   j += '"';
    j += ",\"body\":\"";      j += json_escape(e.implementation); j += '"';
    j += ",\"comment\":\"";   j += json_escape(e.comment);        j += '"';
    j += '}';
    return j;
}

static bool json_to_func(const std::string& json, DataDict::FunctionEntry& e) {
    auto m = json_parse_flat(json);
    if (m.count("type") && m.at("type") != "Function") return false;
    if (m.count("name"))      e.name           = m.at("name");
    if (m.count("container")) e.container      = m.at("container");
    if (m.count("return"))    e.return_type    = m.at("return");
    if (m.count("input"))     e.input_params   = m.at("input");
    if (m.count("body"))      e.implementation = m.at("body");
    if (m.count("comment"))   e.comment        = m.at("comment");
    return true;
}

static std::string view_to_json(const DataDict::ViewEntry& e) {
    std::string j;
    j.reserve(e.sql.size() + 100);
    j += "{\"fmt\":1,\"type\":\"View\"";
    j += ",\"name\":\"";    j += json_escape(e.name);    j += '"';
    j += ",\"sql\":\"";     j += json_escape(e.sql);     j += '"';
    j += ",\"comment\":\""; j += json_escape(e.comment); j += '"';
    j += '}';
    return j;
}

static bool json_to_view(const std::string& json, DataDict::ViewEntry& e) {
    auto m = json_parse_flat(json);
    if (m.count("type") && m.at("type") != "View") return false;
    if (m.count("name"))    e.name    = m.at("name");
    if (m.count("sql"))     e.sql     = m.at("sql");
    if (m.count("comment")) e.comment = m.at("comment");
    return true;
}

// ---------------------------------------------------------------------------
// Build the 1600-byte ADT header block for the DD table.
// ---------------------------------------------------------------------------

static std::vector<std::uint8_t> make_dd_adt_header(std::uint32_t rec_count) {
    // Field descriptors (200 bytes each, 6 fields).
    struct FdSpec { const char* name; std::uint16_t type; std::uint16_t len; std::uint16_t off; };
    static const FdSpec kFds[] = {
        {"OBJ_ID",    11,   4,   5},   // Integer
        {"PARENT_ID", 11,   4,   9},   // Integer
        {"OBJ_TYPE",   4,  20,  13},   // Character
        {"OBJ_NAME",   4, 200,  33},   // Character
        {"OBJ_KEY",    4, 100, 233},   // Character
        {"OBJ_DATA",   5,   9, 333},   // Memo
    };

    std::vector<std::uint8_t> buf(kDdHdrLen, 0);

    // 400-byte base header
    std::memcpy(buf.data(), "Advantage Table", 15);
    p32(buf.data(), 24, rec_count);
    p32(buf.data(), 32, kDdHdrLen);
    p32(buf.data(), 36, kDdRecLen);

    // 200-byte field descriptors
    for (std::uint32_t i = 0; i < kDdNumFlds; ++i) {
        std::uint8_t* fd = buf.data() + kDdHdrBase + i * kDdFldSize;
        std::size_t n = std::strlen(kFds[i].name);
        std::memcpy(fd, kFds[i].name, n);
        p16(fd, 129, static_cast<std::uint16_t>(kFds[i].type));
        p16(fd, 131, kFds[i].off);
        p16(fd, 135, kFds[i].len);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Per-type JSON builders for "simple" types (not trigger/proc/func/view)
// ---------------------------------------------------------------------------

static std::string table_to_json(const DataDict::TableProps& tp) {
    std::string j = "{";
    j += "\"pk\":\"";          j += json_escape(tp.primary_key);   j += '"';
    j += ",\"default_idx\":\""; j += json_escape(tp.default_index); j += '"';
    j += ",\"comment\":\"";    j += json_escape(tp.comment);       j += '"';
    j += ",\"user_defined\":\"";    j += json_escape(tp.user_defined);    j += '"';
    j += ",\"validation_expr\":\""; j += json_escape(tp.validation_expr); j += '"';
    j += ",\"validation_msg\":\"";  j += json_escape(tp.validation_msg);  j += '"';
    // RCB 06/27/2026: Keep optional table-open/cache properties with the
    // table registration record instead of inventing a second DD object type.
    j += ",\"auto_create\":\"";     j += json_escape(tp.auto_create);     j += '"';
    j += ",\"memo_block_size\":\""; j += json_escape(tp.memo_block_size); j += '"';
    j += ",\"caching\":\"";         j += json_escape(tp.caching);         j += '"';
    j += ",\"encryption\":\"";       j += json_escape(tp.encryption);      j += '"';
    j += ",\"permission_level\":\""; j += json_escape(tp.permission_level); j += '"';
    j += ",\"txn_free\":\"";         j += json_escape(tp.txn_free);        j += '"';
    j += '}';
    return j;
}

static std::string link_to_json(const DataDict::LinkEntry& e) {
    std::string j = "{";
    j += "\"path\":\""; j += json_escape(e.path); j += '"';
    j += ",\"user\":\""; j += json_escape(e.user); j += '"';
    j += ",\"pwd\":\"";  j += json_escape(e.pwd);  j += '"';
    j += '}';
    return j;
}

static std::string ri_to_json(const DataDict::RiEntry& e) {
    std::string j = "{";
    j += "\"parent\":\"";     j += json_escape(e.parent);     j += '"';
    j += ",\"child\":\"";     j += json_escape(e.child);      j += '"';
    j += ",\"parent_tag\":\"";j += json_escape(e.parent_tag); j += '"';
    j += ",\"child_tag\":\""; j += json_escape(e.child_tag);  j += '"';
    j += ",\"upd\":\"";       j += json_escape(e.update_opt); j += '"';
    j += ",\"del\":\"";       j += json_escape(e.delete_opt); j += '"';
    j += ",\"fail\":\"";      j += json_escape(e.fail_table); j += '"';
    j += '}';
    return j;
}

// Build JSON from the field_props_ sub-map for one (table, field) pair.
static std::string field_props_to_json(
        const std::unordered_map<std::string,std::string>& kv) {
    std::string j = "{";
    bool first = true;
    for (const auto& [k, v] : kv) {
        if (!first) j += ',';
        j += '"'; j += json_escape(k); j += "\":\""; j += json_escape(v); j += '"';
        first = false;
    }
    j += '}';
    return j;
}

// Build JSON from user_props_ for one user.
static std::string user_props_to_json(
        const std::unordered_map<std::string,std::string>& kv) {
    std::string j = "{";
    bool first = true;
    for (const auto& [k, v] : kv) {
        if (!first) j += ',';
        j += '"'; j += json_escape(k); j += "\":\""; j += json_escape(v); j += '"';
        first = false;
    }
    j += '}';
    return j;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// DataDict::open / create
// ---------------------------------------------------------------------------

util::Result<DataDict> DataDict::open(const std::string& path) {
    DataDict dd;
    dd.path_ = path;
    if (auto r = dd.load_(); !r) return r.error();
    return dd;
}

util::Result<DataDict> DataDict::create(const std::string& path) {
    // Write a fresh ADT-format .add file (header only, 0 records).
    auto hdr = make_dd_adt_header(0);
    {
        auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
        if (!fres) return fres.error();
        auto& file = fres.value();
        if (auto r = file.write_at(0, hdr.data(), hdr.size()); !r) return r.error();
        if (auto r = file.sync(); !r) return r.error();
    }

    // Write an empty ADM memo file (.am).
    {
        auto amp = am_path(path);
        auto amf = platform::File::open(amp, platform::OpenMode::CreateRW);
        if (!amf) return amf.error();
        std::vector<std::uint8_t> am_hdr(kAmBlock, 0);
        p32(am_hdr.data(), 20, 1u);  // next_avail = 1 (block 0 = header)
        if (auto r = amf.value().write_at(0, am_hdr.data(), am_hdr.size()); !r)
            return r.error();
        if (auto r = amf.value().sync(); !r) return r.error();
    }

    DataDict dd;
    dd.path_ = path;
    return dd;
}

// Trim trailing spaces and embedded nulls from a fixed-length CHAR field.
static std::string trim_char(const std::string& buf,
                              std::size_t off, std::size_t maxlen) {
    std::string s = buf.substr(off, maxlen);
    auto p = s.find_last_not_of(" \0", std::string::npos, 2);
    return (p == std::string::npos) ? "" : s.substr(0, p + 1);
}

util::Result<void> DataDict::load_add_binary_(const std::string& buf) {
    // =========================================================================
    // ADS binary .add file format.
    //
    // The .add file is an Advantage Data Table (ADT) whose header (variable
    // length, field at 0x20) describes the column layout, and whose body is a
    // fixed-length record array.
    //
    // Record layout (rec_len = 524 bytes):
    //   [0]        status: 0x04=active, 0x05=deleted
    //   [1..4]     null bitmap (zero in all observed files)
    //   [5..8]     Object ID   (uint32 LE, AutoInc)
    //   [9..12]    Parent ID   (uint32 LE)
    //   [13..22]   Object Type (CHAR(10), space-padded)
    //   [23..222]  Object Name (CHAR(200), space-padded)
    //   [223..497] Property    (VarChar(275): uint16 LE plen at [223], data at
    //                           [225..225+plen-1]; 0xFFFF = SQL NULL)
    //   [498..506] More Property (Binary(9), memo block pointer for overflow)
    //   [507..510] Info1       (uint32 LE)
    //   [511..514] Info2       (uint32 LE)
    //   [515..523] Comment     (Memo(9), block pointer)
    //
    // Header fields used for write-back:
    //   0x18-0x1B: total record count (active + deleted)
    //   0x50-0x53: next ObjID to assign (= max active ObjID + 1)
    //
    // -------------------------------------------------------------------------
    // Property VarChar layout (275 bytes = 2-byte plen + 273 data bytes):
    //
    //   The 273-byte data area is split into two zones:
    //   - [0 .. plen-1]    object-type-specific inline data (varies by type)
    //   - [plen .. 272]    extra area (mostly 0xFF padding; for User records
    //                       this zone holds the group-membership token table)
    //
    //   For User records the extra area starts at offset plen and has the
    //   following layout (reversed-engineered from the ACE binary format):
    //
    //     [plen+0 .. plen+1]  count_field (uint16 LE)
    //                           0xFFFF  → no property-byte groups
    //                           N×4     → N groups are stored
    //     [plen+2 .. plen+2+N×4-1]
    //                         N×4-byte group tokens (see Third pass below)
    //     [...]               0x00 0x00 end marker + 0xFF padding
    //
    // -------------------------------------------------------------------------
    // Group-membership storage — two mechanisms coexist in the same file:
    //
    //   1. Permission records (parent_id=user, info1=group, info2=0x80000000):
    //      Written by AdsDDAddUserToGroup in SAP ACE v8+ and by OpenADS for all
    //      new memberships.  Decoded in the Second pass below.
    //
    //   2. User property-byte XOR tokens:
    //      An older per-database XOR encoding in the User property extra area.
    //      Present in databases maintained by pre-v8 SAP ACE or set up via
    //      AdsDDSetUserProperty(1102).  Decoded in the Third pass below.
    //
    //   Both sources are unioned into memberships_; duplicates are harmless.
    // =========================================================================
    if (buf.size() < 40)
        return util::Error{5000, 0, "ADD file too small", path_};

    uint32_t hdr_len = le32(buf, 0x20);
    uint32_t rec_len = le32(buf, 0x24);

    if (rec_len == 0 || hdr_len > buf.size())
        return util::Error{5000, 0, "ADD header corrupt", path_};

    binary_format_  = true;
    binary_hdr_len_ = hdr_len;
    binary_rec_len_ = rec_len;
    binary_hdr_     = buf.substr(0, hdr_len);

    // Pre-load companion .am memo file (holds overflow SP/function SQL bodies).
    // Block_size = 8 bytes; byte_offset = block_num * 8.
    std::string am_buf;
    {
        std::string am_path = path_;
        auto dot = am_path.rfind('.');
        if (dot != std::string::npos) {
            am_path = am_path.substr(0, dot) + ".am";
            auto amf_res = platform::File::open(am_path, platform::OpenMode::ReadOnly);
            if (amf_res) {
                auto am_file = std::move(amf_res).value();
                auto sz_res = am_file.size();
                if (sz_res && sz_res.value() > 0) {
                    am_buf.resize(static_cast<std::size_t>(sz_res.value()));
                    auto rr = am_file.read_at(0, am_buf.data(),
                                              static_cast<std::size_t>(sz_res.value()));
                    if (rr && rr.value() < am_buf.size()) am_buf.resize(rr.value());
                }
            }
        }
    }

    // Append .am continuation bytes referenced by a record's more_property field.
    // more_property layout (9 bytes): [4-byte LE block_num][4-byte LE data_len][0x00]
    auto append_am = [&](std::string& field,
                         const std::array<std::uint8_t, 9>& mp) {
        auto am_block = static_cast<uint32_t>(mp[0])
                      | (static_cast<uint32_t>(mp[1]) << 8)
                      | (static_cast<uint32_t>(mp[2]) << 16)
                      | (static_cast<uint32_t>(mp[3]) << 24);
        auto am_len   = static_cast<uint32_t>(mp[4])
                      | (static_cast<uint32_t>(mp[5]) << 8)
                      | (static_cast<uint32_t>(mp[6]) << 16)
                      | (static_cast<uint32_t>(mp[7]) << 24);
        if (am_block == 0 || am_len == 0 || am_buf.empty()) return;
        std::size_t am_off = static_cast<std::size_t>(am_block) * 8;
        if (am_off >= am_buf.size()) return;
        std::size_t readable = std::min<std::size_t>(am_len, am_buf.size() - am_off);
        std::string cont = am_buf.substr(am_off, readable);
        // Strip trailing garbage: scan backward to the last valid SQL byte
        // (printable ASCII 0x20-0x7E or horizontal/vertical whitespace).
        std::size_t lt = cont.size();
        while (lt > 0) {
            unsigned char uc = static_cast<unsigned char>(cont[lt - 1]);
            if ((uc >= 0x20u && uc <= 0x7Eu) || uc == '\t' || uc == '\n' || uc == '\r') break;
            --lt;
        }
        if (lt < cont.size()) cont.resize(lt);
        auto l = cont.find_last_not_of(" \t\r\n");
        if (l != std::string::npos) cont.resize(l + 1);
        field += cont;
    };

    std::size_t total = (buf.size() - hdr_len) / rec_len;
    binary_recs_.reserve(total);

    // obj_id → (name, type) — built incrementally for in-pass lookups.
    std::unordered_map<uint32_t, std::string> id_to_name;
    std::unordered_map<uint32_t, std::string> id_to_type;

    for (std::size_t i = 0; i < total; ++i) {
        std::size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;
        if (static_cast<uint8_t>(buf[base]) != 0x04u) continue;
        auto obj_id = le32(buf, base + 5);
        auto obj_type = trim_char(buf, base + 13, 10);
        auto obj_name = trim_char(buf, base + 23, 200);
        if (!obj_name.empty()) {
            id_to_name[obj_id] = obj_name;
            id_to_type[obj_id] = obj_type;
        }
    }

    for (std::size_t i = 0; i < total; ++i) {
        std::size_t base = hdr_len + i * rec_len;
        if (base + rec_len > buf.size()) break;

        BinaryRecord r;
        uint8_t status = static_cast<uint8_t>(buf[base]);
        r.active     = (status == 0x04);
        r.obj_id     = le32(buf, base + 5);
        r.parent_id  = le32(buf, base + 9);
        r.obj_type   = trim_char(buf, base + 13, 10);
        r.obj_name   = trim_char(buf, base + 23, 200);

        // Property VarChar
        uint16_t plen = le16(buf, base + 223);
        if (plen == 0xFFFFu) {
            r.prop_null = true;
        } else {
            r.prop_null = false;
            if (plen > 0 && base + 225 + plen <= buf.size())
                r.property = buf.substr(base + 225, plen);
        }

        // More Property (9 bytes)
        for (std::size_t j = 0; j < 9; ++j)
            r.more_property[j] = static_cast<uint8_t>(buf[base + 498u + j]);

        r.info1 = le32(buf, base + 507);
        r.info2 = le32(buf, base + 511);

        // Comment (9 bytes)
        for (std::size_t j = 0; j < 9; ++j)
            r.comment[j] = static_cast<uint8_t>(buf[base + 515u + j]);

        binary_recs_.push_back(std::move(r));

        // Populate in-memory maps from active records we understand.
        const BinaryRecord& rec = binary_recs_.back();
        if (!rec.active) continue;

        if (rec.obj_type == "Table" && !rec.obj_name.empty() &&
            !rec.prop_null && !rec.property.empty()) {
            // OpenADS NUL-delimited format: path\0primary_key\0default_index\0comment
            // SAP binary format: path NUL-terminated (additional bytes are binary/unknown)
            auto parts = split_nul(rec.property);
            std::string path = parts.empty() ? rec.property : parts[0];
            if (!path.empty()) tables_[rec.obj_name] = path;
            // Only read extra properties if format looks like NUL-delimited text
            // (parts.size() > 1 means there are additional NUL-separated strings)
            if (parts.size() > 1 && !parts[1].empty()) {
                TableProps& tp = table_props_[rec.obj_name];
                if (parts.size() > 1) tp.primary_key   = parts[1];
                if (parts.size() > 2) tp.default_index = parts[2];
                if (parts.size() > 3) tp.comment        = parts[3];
            }

        } else if (rec.obj_type == "Index" && !rec.obj_name.empty() &&
                   !rec.prop_null && !rec.property.empty()) {
            // Find parent table alias for IndexEntry.table_alias.
            std::string tbl_alias;
            for (const auto& r2 : binary_recs_) {
                if (r2.active && r2.obj_type == "Table" &&
                    r2.obj_id == rec.parent_id) {
                    tbl_alias = r2.obj_name;
                    break;
                }
            }
            std::string idx_path = rec.property;
            auto nul = idx_path.find('\0');
            if (nul != std::string::npos) idx_path.resize(nul);
            if (!tbl_alias.empty() && !idx_path.empty()) {
                // RCB 07/16/2026: name the object rather than brace-init a
                // subset — IndexEntry grew per-tag metadata fields, and clang
                // -Werror=missing-field-initializers rejects a partial {a,b,{}}.
                IndexEntry e;
                e.table_alias = tbl_alias;
                e.index_path = idx_path;
                indexes_.push_back(std::move(e));
            }

        } else if (rec.obj_type == "User" && !rec.obj_name.empty()) {
            // RCB 2026-06-27: Imported SAP user object names can arrive in
            // mixed case; store them folded so all user lookups share one key.
            users_.insert(ci_name(rec.obj_name));

        } else if (rec.obj_type == "Group" && !rec.obj_name.empty()) {
            groups_.insert(rec.obj_name);

        } else if ((rec.obj_type == "Procedure" || rec.obj_type == "StoredProc") &&
                   !rec.obj_name.empty()) {
            ProcEntry e;
            e.name = rec.obj_name;

            if (!rec.prop_null && !rec.property.empty() &&
                static_cast<uint8_t>(rec.property[0]) == 0x09) {
                // OpenADS proprietary JSON-in-.am format (sentinel 0x09)
                auto jtext = read_am_json(am_buf, rec.more_property);
                if (!jtext.empty()) json_to_proc(jtext, e);
                procs_[e.name] = std::move(e);
                continue;
            }

            const std::size_t PS = base + 225;
            const std::size_t PL = 273;

            // OpenADS legacy format: plen=273, property[0]=NUL, property[1..272]=SQL body start.
            // .am layout: [SQL continuation]\0[input_params]\0[output_params]\0
            // SAP-original format: plen=param_len (< 273), property[0..plen-1]=input_params,
            //   followed by 0xFF markers + binary header + CRLF + SQL body.
            bool is_openads = (!rec.prop_null && plen == 273 &&
                               PS + PL <= buf.size() &&
                               static_cast<uint8_t>(buf[PS]) == 0x00 &&
                               (PL < 2 || static_cast<uint8_t>(buf[PS + 1]) >= 0x20));

            if (is_openads) {
                // Read inline SQL body start from property[1..272], strip trailing whitespace/NUL.
                {
                    std::string body_start = buf.substr(PS + 1, PL - 1);
                    std::size_t l = body_start.size();
                    while (l > 0) {
                        unsigned char c = static_cast<unsigned char>(body_start[l - 1]);
                        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0') --l;
                        else break;
                    }
                    body_start.resize(l);
                    e.procedure = std::move(body_start);
                }
                // Parse .am with NUL-delimited segments: body_cont\0in_params\0out_params\0
                auto am_block = static_cast<uint32_t>(rec.more_property[0])
                              | (static_cast<uint32_t>(rec.more_property[1]) <<  8)
                              | (static_cast<uint32_t>(rec.more_property[2]) << 16)
                              | (static_cast<uint32_t>(rec.more_property[3]) << 24);
                auto am_len   = static_cast<uint32_t>(rec.more_property[4])
                              | (static_cast<uint32_t>(rec.more_property[5]) <<  8)
                              | (static_cast<uint32_t>(rec.more_property[6]) << 16)
                              | (static_cast<uint32_t>(rec.more_property[7]) << 24);
                if (am_block > 0 && am_len > 0) {
                    std::size_t am_off = static_cast<std::size_t>(am_block) * 8;
                    if (am_off < am_buf.size()) {
                        std::size_t readable = std::min<std::size_t>(
                            am_len, am_buf.size() - am_off);
                        std::string chunk = am_buf.substr(am_off, readable);
                        auto n0 = chunk.find('\0');
                        if (n0 != std::string::npos) {
                            // Body continuation up to first NUL — strip binary padding.
                            std::string cont = chunk.substr(0, n0);
                            std::size_t lt = cont.size();
                            while (lt > 0) {
                                unsigned char uc = static_cast<unsigned char>(cont[lt - 1]);
                                if ((uc >= 0x20u && uc <= 0x7Eu) ||
                                    uc == '\t' || uc == '\n' || uc == '\r') break;
                                --lt;
                            }
                            cont.resize(lt);
                            auto lc = cont.find_last_not_of(" \t\r\n");
                            if (lc != std::string::npos) cont.resize(lc + 1);
                            else cont.clear();
                            e.procedure += cont;
                            // input_params between NUL[0] and NUL[1].
                            auto n1 = chunk.find('\0', n0 + 1);
                            if (n1 != std::string::npos) {
                                e.input_params = chunk.substr(n0 + 1, n1 - (n0 + 1));
                                // output_params between NUL[1] and NUL[2].
                                auto n2 = chunk.find('\0', n1 + 1);
                                e.output_params = (n2 != std::string::npos)
                                    ? chunk.substr(n1 + 1, n2 - (n1 + 1))
                                    : chunk.substr(n1 + 1);
                            }
                        } else {
                            // No NUL separator — backward-scan for binary padding.
                            std::size_t lt = chunk.size();
                            while (lt > 0) {
                                unsigned char uc = static_cast<unsigned char>(chunk[lt - 1]);
                                if ((uc >= 0x20u && uc <= 0x7Eu) ||
                                    uc == '\t' || uc == '\n' || uc == '\r') break;
                                --lt;
                            }
                            chunk.resize(lt);
                            auto lc = chunk.find_last_not_of(" \t\r\n");
                            if (lc != std::string::npos) chunk.resize(lc + 1);
                            e.procedure += chunk;
                        }
                    }
                }
            } else {
                // SAP-original: input_params at property[0..plen-1], body via 0xFF/CRLF scan.
                if (!rec.prop_null && !rec.property.empty()) {
                    e.input_params = rec.property;
                    auto nul = e.input_params.find('\0');
                    if (nul != std::string::npos) e.input_params.resize(nul);
                }
                if (PS + PL <= buf.size()) {
                    std::size_t pos = rec.prop_null ? 0u : static_cast<std::size_t>(plen);
                    while (pos < PL && static_cast<uint8_t>(buf[PS + pos]) == 0xFF) ++pos;
                    // Output params: LE16 length + NUL-terminated string, if present here.
                    // Detected by: 2-byte LE16 > 0 AND all bytes in the string are printable ASCII.
                    // If the first byte is < 0x20 (e.g. 0x04 for invoke_option), it is NOT output_params.
                    if (pos + 2 < PL) {
                        uint16_t olen = static_cast<uint16_t>(
                            static_cast<uint8_t>(buf[PS + pos]) |
                            (static_cast<uint16_t>(static_cast<uint8_t>(buf[PS + pos + 1])) << 8));
                        if (olen > 0 && olen < 500 && pos + 2 + olen <= PL) {
                            bool looks_text = true;
                            for (std::size_t k = pos + 2; k < pos + 2 + olen && k < PL; ++k) {
                                unsigned char c = static_cast<unsigned char>(buf[PS + k]);
                                if (c != '\0' && (c < 0x20u || c > 0x7Eu)) { looks_text = false; break; }
                            }
                            if (looks_text) {
                                e.output_params = buf.substr(PS + pos + 2, olen);
                                auto nul = e.output_params.find('\0');
                                if (nul != std::string::npos) e.output_params.resize(nul);
                                pos += 2 + olen;
                                while (pos < PL && static_cast<uint8_t>(buf[PS + pos]) == 0xFF) ++pos;
                            }
                        }
                    }
                    // RCB 07/16/2026: after the output-params lstr the SAP
                    // layout is STRUCTURED (verified by hex-dump of pmsys.add):
                    //   [le16 invoke_option][le32 reserved][le16 body_len][body…]
                    // body_len counts the WHOLE body; the inline zone holds the
                    // first PL-pos bytes and the .am continuation the rest.
                    // The old code scanned forward to the first CRLF to find
                    // the body start — but the first CRLF is the END of the
                    // body's FIRST LINE, so every proc lost its opening line
                    // (e.g. sp_GetPhysicalPath lost "DECLARE @sql STRING;").
                    bool structured = false;
                    if (pos + 8 <= PL) {
                        uint16_t invoke = le16(buf, PS + pos);
                        uint16_t blen   = le16(buf, PS + pos + 6);
                        if (invoke < 0x100 && blen > 0 && blen != 0xFFFFu) {
                            std::size_t bpos     = pos + 8;
                            std::size_t inline_n = std::min<std::size_t>(
                                blen, PL - bpos);
                            e.procedure = buf.substr(PS + bpos, inline_n);
                            std::size_t remaining = blen - inline_n;
                            if (remaining > 0) {
                                // Continuation: exactly `remaining` bytes from
                                // the .am block — no printable-scan heuristics
                                // needed because the length is authoritative.
                                auto am_block =
                                      static_cast<uint32_t>(rec.more_property[0])
                                    | (static_cast<uint32_t>(rec.more_property[1]) <<  8)
                                    | (static_cast<uint32_t>(rec.more_property[2]) << 16)
                                    | (static_cast<uint32_t>(rec.more_property[3]) << 24);
                                std::size_t am_off =
                                    static_cast<std::size_t>(am_block) * 8;
                                if (am_block > 0 && am_off < am_buf.size()) {
                                    std::size_t take = std::min<std::size_t>(
                                        remaining, am_buf.size() - am_off);
                                    e.procedure += am_buf.substr(am_off, take);
                                }
                            }
                            auto lc = e.procedure.find_last_not_of(" \t\r\n\0",
                                                                   std::string::npos, 5);
                            if (lc != std::string::npos) e.procedure.resize(lc + 1);
                            else e.procedure.clear();
                            structured = true;
                        }
                    }
                    if (!structured) {
                        // Legacy fallback: CRLF scan (pre-structured layouts).
                        for (; pos + 1 < PL; ++pos) {
                            if (static_cast<uint8_t>(buf[PS + pos])   == 0x0D &&
                                static_cast<uint8_t>(buf[PS + pos+1]) == 0x0A) break;
                        }
                        if (pos + 1 < PL) {
                            std::size_t end = PL;
                            for (std::size_t j = pos; j < PL; ++j)
                                if (buf[PS + j] == '\0') { end = j; break; }
                            std::string body = buf.substr(PS + pos, end - pos);
                            auto f = body.find_first_not_of(" \t\r\n");
                            if (f != std::string::npos) body = body.substr(f);
                            auto l = body.find_last_not_of(" \t\r\n");
                            if (l != std::string::npos) body.resize(l + 1);
                            e.procedure = std::move(body);
                        }
                        // SAP .am body is a NUL-terminated C string; strip from
                        // first NUL.
                        const auto body_end = e.procedure.size();
                        append_am(e.procedure, rec.more_property);
                        auto nul = e.procedure.find('\0', body_end);
                        if (nul != std::string::npos) e.procedure.resize(nul);
                        auto lc = e.procedure.find_last_not_of(" \t\r\n");
                        if (lc != std::string::npos) e.procedure.resize(lc + 1);
                        else e.procedure.clear();
                    }
                }
            }
            procs_[e.name] = std::move(e);

        } else if (rec.obj_type == "Function" && !rec.obj_name.empty()) {
            // Preserve the raw plen so save can restore the correct preamble size.
            binary_recs_.back().prop_plen = (plen == 0xFFFFu) ? 0u : plen;
            FunctionEntry e;
            e.name = rec.obj_name;

            if (!rec.prop_null && !rec.property.empty() &&
                static_cast<uint8_t>(rec.property[0]) == 0x0A) {
                // OpenADS proprietary JSON-in-.am format (sentinel 0x0A)
                auto jtext = read_am_json(am_buf, rec.more_property);
                if (!jtext.empty()) json_to_func(jtext, e);
                functions_[e.name] = std::move(e);
                continue;
            }

            // Property area layout (273 bytes at base+225..base+497):
            // [plen binary metadata] [6×0xFF] [le16+rettype\0] [le16+inparams\0] [le16+body]
            {
                const std::size_t PS = base + 225;
                const std::size_t PL = 273;
                if (PS + PL <= buf.size()) {
                    std::size_t pos = rec.prop_null ? 0u : static_cast<std::size_t>(plen);
                    while (pos < PL && static_cast<uint8_t>(buf[PS + pos]) == 0xFF) ++pos;

                    // read_lstr: reads a length-prefixed string from the property area.
                    // When the body's slen > remaining space the string is SPLIT:
                    // the inline area holds the first (PL-pos) bytes; the .am file
                    // holds the continuation.  Read as many bytes as fit.
                    auto read_lstr = [&](std::string& out) {
                        if (pos + 2 > PL) return;
                        uint16_t slen = static_cast<uint16_t>(
                            static_cast<uint8_t>(buf[PS + pos]) |
                            (static_cast<uint16_t>(static_cast<uint8_t>(buf[PS + pos + 1])) << 8));
                        pos += 2;
                        if (slen == 0 || slen == 0xFFFF) return;
                        std::size_t readable = std::min<std::size_t>(slen, PL - pos);
                        if (readable == 0) return;
                        out = buf.substr(PS + pos, readable);
                        pos += readable;
                        auto nul = out.find('\0');
                        if (nul != std::string::npos) out.resize(nul);
                    };

                    read_lstr(e.return_type);
                    read_lstr(e.input_params);
                    read_lstr(e.implementation);
                    auto f = e.implementation.find_first_not_of(" \t\r\n");
                    if (f != std::string::npos) e.implementation = e.implementation.substr(f);
                    auto l = e.implementation.find_last_not_of(" \t\r\n");
                    if (l != std::string::npos) e.implementation.resize(l + 1);
                }
            }
            // If the .am slot was previously written as JSON-in-.am (by save_add_binary_)
            // but the binary sentinel byte was not updated (e.g., import/partial write),
            // recover by parsing the JSON instead of appending it as body continuation.
            {
                auto jtext = read_am_json(am_buf, rec.more_property);
                if (!jtext.empty() && jtext.front() == '{') {
                    FunctionEntry je;
                    je.name = e.name;
                    if (json_to_func(jtext, je) && !je.implementation.empty()) {
                        e = std::move(je);  // use JSON data (complete and correct)
                    } else {
                        append_am(e.implementation, rec.more_property);
                    }
                } else {
                    append_am(e.implementation, rec.more_property);
                }
            }
            functions_[e.name] = std::move(e);

        } else if (rec.obj_type == "Trigger" && !rec.obj_name.empty()) {
            TriggerEntry e;
            e.name        = rec.obj_name;
            e.table_alias = id_to_name.count(rec.parent_id) ? id_to_name.at(rec.parent_id) : "";

            if (!rec.prop_null && !rec.property.empty() &&
                static_cast<uint8_t>(rec.property[0]) == 0x08) {
                // OpenADS proprietary JSON-in-.am format (sentinel byte 0x08)
                auto am_block = static_cast<uint32_t>(rec.more_property[0])
                              | (static_cast<uint32_t>(rec.more_property[1]) <<  8)
                              | (static_cast<uint32_t>(rec.more_property[2]) << 16)
                              | (static_cast<uint32_t>(rec.more_property[3]) << 24);
                auto am_len   = static_cast<uint32_t>(rec.more_property[4])
                              | (static_cast<uint32_t>(rec.more_property[5]) <<  8)
                              | (static_cast<uint32_t>(rec.more_property[6]) << 16)
                              | (static_cast<uint32_t>(rec.more_property[7]) << 24);
                if (am_block > 0 && am_len > 0 && !am_buf.empty()) {
                    std::size_t am_off = static_cast<std::size_t>(am_block) * 8;
                    if (am_off + am_len <= am_buf.size()) {
                        std::string json_text = am_buf.substr(am_off, am_len);
                        json_to_trigger(json_text, e);
                    }
                }
            } else if (!rec.prop_null && !rec.property.empty() &&
                static_cast<uint8_t>(rec.property[0]) >= 0x20) {
                // OpenADS NUL-delimited format:
                //   OLD (7 parts): table_alias\0event_mask\0priority\0enabled\0container\0procedure\0comment
                //   NEW (8 parts): table_alias\0event_mask\0timing\0priority\0enabled\0container\0procedure\0comment
                //   NEW (9 parts): ...same + options
                auto parts = split_nul(rec.property);
                // parts[0] is the authoritative table alias (stored in the NUL blob).
                // The parent_id lookup gives "Database" for newly-created triggers,
                // so always prefer parts[0] when it is non-empty.
                if (parts.size() > 0 && !parts[0].empty()) e.table_alias = parts[0];
                if (parts.size() > 1) try { e.event_mask = static_cast<std::uint32_t>(std::stoul(parts[1])); } catch (...) {}
                if (parts.size() >= 8) {
                    // New format with timing in slot 2
                    try { e.timing   = static_cast<std::uint32_t>(std::stoul(parts[2])); } catch (...) {}
                    try { e.priority = static_cast<std::uint32_t>(std::stoi(parts[3])); } catch (...) {}
                    if (parts.size() > 4) e.enabled   = (parts[4] == "1");
                    if (parts.size() > 5) e.container = parts[5];
                    if (parts.size() > 6) e.procedure = parts[6];
                    if (parts.size() > 7) e.comment   = parts[7];
                    if (parts.size() > 8) try { e.options = static_cast<std::uint32_t>(std::stoul(parts[8])); } catch (...) {}
                } else {
                    // Old format without timing (timing defaults to 0)
                    try { e.priority = static_cast<std::uint32_t>(std::stoi(parts[2])); } catch (...) {}
                    if (parts.size() > 3) e.enabled   = (parts[3] == "1");
                    if (parts.size() > 4) e.container = parts[4];
                    if (parts.size() > 5) e.procedure = parts[5];
                    if (parts.size() > 6) e.comment   = parts[6];
                }
                // Long bodies are written to .am; append any continuation.
                append_am(e.container, rec.more_property);
            } else {
                // SAP binary .add format for Trigger records (rec_len=524):
                //   Property area (273 bytes at base+225..base+497):
                //     [0..3]   event  (LE uint32): 1=INSERT 2=UPDATE 3=DELETE
                //     [4..5]   0x04 (constant)
                //     [6..7]   timing (LE uint16): 1=BEFORE 2=INSTEAD_OF 4=AFTER
                //     [16..17] inline body length (LE uint16)
                //     [18..]   inline body SQL text (up to 255 bytes)
                if (!rec.prop_null && rec.property.size() >= 4) {
                    e.event_mask = static_cast<uint32_t>(
                        (static_cast<uint8_t>(rec.property[0]))       |
                        (static_cast<uint8_t>(rec.property[1]) <<  8) |
                        (static_cast<uint8_t>(rec.property[2]) << 16) |
                        (static_cast<uint8_t>(rec.property[3]) << 24));
                }
                // SAP binary triggers: plen covers ONLY the event u32, so
                // rec.property is 4 bytes — timing/options/body live in the
                // undeclared part of the 273-byte property area and must be
                // read from buf[] directly (oracle-verified on pmsys.add,
                // 6/6 triggers match SAP system.triggers):
                //   [4..5]   0x0004
                //   [6..9]   timing  (LE u32): 1=BEFORE 2=INSTEAD_OF 4=AFTER
                //   [10..11] 0x0004
                //   [12..15] options (LE u32): 0x01 WANT_VALUES | 0x02 MEMOS
                //   [16..17] inline body length (LE uint16)
                //   [18..272] up to 255 bytes of SQL text (+ .am overflow)
                {
                    const std::size_t TPS = base + 225;
                    const std::size_t TPL = 273;
                    if (TPS + TPL <= buf.size()) {
                        e.timing = static_cast<uint32_t>(
                            static_cast<uint8_t>(buf[TPS + 6]) |
                            (static_cast<uint8_t>(buf[TPS + 7]) << 8));
                        e.options = static_cast<uint32_t>(
                            static_cast<uint8_t>(buf[TPS + 12]) |
                            (static_cast<uint8_t>(buf[TPS + 13]) << 8));
                        uint16_t body_len = static_cast<uint16_t>(
                            static_cast<uint8_t>(buf[TPS + 16]) |
                            (static_cast<uint8_t>(buf[TPS + 17]) << 8));
                        std::size_t read_len = (body_len > 0)
                            ? std::min<std::size_t>(body_len, TPL - 18)
                            : (TPL - 18);
                        std::string body = buf.substr(TPS + 18, read_len);
                        // Trim leading whitespace and NUL padding now; trailing trim
                        // is deferred until after any .am continuation is appended so
                        // we don't accidentally eat functional spaces at the boundary.
                        auto nul = body.find('\0');
                        if (nul != std::string::npos) body.resize(nul);
                        auto first = body.find_first_not_of(" \t\r\n");
                        if (first != std::string::npos) body = body.substr(first);
                        e.container = std::move(body);
                    }
                }
                // Append .am continuation (remainder when body > 255 bytes)
                append_am(e.container, rec.more_property);
                // Trim trailing whitespace/SAP alignment padding from the complete body.
                {
                    auto last = e.container.find_last_not_of(" \t\r\n");
                    if (last != std::string::npos) e.container.resize(last + 1);
                    else e.container.clear();
                }
                // SAP binary format does not store an enabled flag; default (true) stands.
            }

            triggers_[e.table_alias + "::" + e.name] = std::move(e);

        } else if (rec.obj_type == "Relation" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            RiEntry e;
            e.name = rec.obj_name;
            // SAP binary .add stores only 4 bytes (parent table obj_id) in the declared
            // property; the rest of the RI data lives in the undeclared part of the 273-byte
            // property area:
            //   +0:  parent_table_id  [4 LE uint32]
            //   +4:  0x04 0x00
            //   +6:  parent_pk_key_id [4 LE uint32]  (Key record → tag name)
            //   +10: 0x04 0x00
            //   +12: child_table_id   [4 LE uint32]
            //   +16: 0x04 0x00
            //   +18: child_fk_key_id  [4 LE uint32]  (Key record → tag name)
            //   +22: 0x01 0x00
            //   +24: update_rule      [1 byte]  1=Restrict 2=Cascade 3=SetNull
            //   +25: delete_rule      [1 byte]
            if (binary_format_ && rec.property.size() == 4 &&
                base + 225 + 26 <= buf.size()) {
                auto update_rule_str = [](uint8_t v) -> std::string {
                    if (v == 1) return "Cascade";
                    if (v == 3) return "SetNull";
                    return "Restrict";
                };
                auto delete_rule_str = [](uint8_t v) -> std::string {
                    if (v == 2) return "Cascade";
                    if (v == 3) return "SetNull";
                    return "Restrict";
                };
                auto look = [&](uint32_t id) -> std::string {
                    auto it = id_to_name.find(id);
                    return (it != id_to_name.end()) ? it->second : "";
                };
                uint32_t par_tbl  = le32(buf, base + 225 +  0);
                uint32_t par_key  = le32(buf, base + 225 +  6);
                uint32_t chi_tbl  = le32(buf, base + 225 + 12);
                uint32_t chi_key  = le32(buf, base + 225 + 18);
                uint8_t  upd = static_cast<uint8_t>(buf[base + 225 + 24]);
                uint8_t  del = static_cast<uint8_t>(buf[base + 225 + 25]);
                e.parent     = look(par_tbl);
                e.child      = look(chi_tbl);
                e.parent_tag = look(par_key);
                e.child_tag  = look(chi_key);
                e.update_opt = update_rule_str(upd);
                e.delete_opt = delete_rule_str(del);
            } else {
                auto parts = split_nul(rec.property);
                while (parts.size() < 7) parts.emplace_back();
                e.parent     = parts[0];
                e.child      = parts[1];
                e.parent_tag = parts[2];
                e.child_tag  = parts[3];
                e.update_opt = parts[4];
                e.delete_opt = parts[5];
                e.fail_table = parts[6];
            }
            ri_[e.name] = std::move(e);

        } else if (rec.obj_type == "View" && !rec.obj_name.empty() &&
                   !rec.prop_null) {
            ViewEntry e;
            e.name = rec.obj_name;
            if (!rec.property.empty() &&
                static_cast<uint8_t>(rec.property[0]) == 0x0B) {
                // OpenADS proprietary JSON-in-.am format (sentinel 0x0B)
                auto jtext = read_am_json(am_buf, rec.more_property);
                if (!jtext.empty()) json_to_view(jtext, e);
            } else {
                auto parts = split_nul(rec.property);
                while (parts.size() < 2) parts.emplace_back();
                e.comment = parts[0];
                e.sql     = parts[1];
            }
            views_[e.name] = std::move(e);

        } else if (rec.obj_type == "Field" && !rec.obj_name.empty()) {
            // Populate field_props_ so system.permissions can enumerate fields.
            // parent_id → table's obj_id; info1 → ordinal position (1-based).
            auto tbl_it = id_to_name.find(rec.parent_id);
            if (tbl_it != id_to_name.end()) {
                const std::string& tbl = tbl_it->second;
                auto& fp = field_props_[tbl][rec.obj_name];
                if (fp.find("ordinal") == fp.end())
                    fp["ordinal"] = std::to_string(rec.info1);
                // SAP binary Field-record decode (oracle-verified against
                // system.columns on pmsys.add, 2026-07-19):
                //   - inline property (when not SQL NULL) = the DEFAULT
                //     value expression, NUL-terminated ("FALSE", "now()").
                //   - the extra area right after it (offset 0 when the
                //     property is NULL) starts with a uint16 null-valid
                //     flag: 0x0000 = NOT NULL, 0xFFFF = nullable. OpenADS
                //     itself never writes "Field" records (it uses
                //     FieldProp JSON), so a strict 0x0000 match is safe.
                if (!rec.prop_null && !rec.property.empty() &&
                    fp.find("default") == fp.end()) {
                    std::string defv = rec.property;
                    auto z = defv.find('\0');
                    if (z != std::string::npos) defv.resize(z);
                    if (!defv.empty()) fp["default"] = defv;
                }
                std::size_t xoff =
                    base + 225 + (rec.prop_null ? 0 : rec.property.size());
                if (xoff + 2 <= base + 498 && xoff + 2 <= buf.size()) {
                    if (le16(buf, xoff) == 0x0000u) fp["required"] = "T";
                }
            }
        }
    }

    // SAP object-type string → numeric code used in system.permissions.
    auto type_code = [](const std::string& t) -> int {
        if (t == "Table")      return 1;   // ADS_DD_TABLE_OBJECT
        if (t == "Field")      return 4;   // ADS_DD_FIELD_OBJECT
        if (t == "View")       return 6;   // ADS_DD_VIEW_OBJECT
        if (t == "User")       return 8;   // ADS_DD_USER_OBJECT
        if (t == "Group")      return 9;   // ADS_DD_USER_GROUP_OBJECT
        if (t == "StoredProc") return 10;  // ADS_DD_PROCEDURE_OBJECT
        if (t == "Database")   return 11;  // ADS_DD_DATABASE_OBJECT
        if (t == "Link")       return 12;  // ADS_DD_LINK_OBJECT
        if (t == "Function")   return 18;  // ADS_DD_FUNCTION_OBJECT
        return 0;
    };

    // -------------------------------------------------------------------------
    // Second pass: Permission records.
    //
    // Permission record layout:
    //   obj_type  = "Permission"
    //   obj_name  = sprintf("%08X", info1)   (hex of target obj_id)
    //   parent_id = granting principal (User or Group obj_id)
    //   info1     = target object obj_id
    //   info2     = permission bitmask; 0x80000000 = INHERIT (full/group-member)
    //
    // Two interpretations depending on the target's type:
    //   target type == "Group" → user-group membership
    //     parent (User) is a member of target (Group).
    //     SAP ACE creates these via AdsDDAddUserToGroup.  OpenADS uses the same
    //     format for all new write-path group memberships.
    //   target type != "Group" → ACL entry (table/object permission)
    for (const auto& rec : binary_recs_) {
        if (!rec.active || rec.obj_type != "Permission") continue;
        auto principal_it = id_to_name.find(rec.parent_id);
        if (principal_it == id_to_name.end()) continue;
        const std::string& principal = principal_it->second;

        auto target_type_it = id_to_type.find(rec.info1);
        if (target_type_it == id_to_type.end()) continue;
        const std::string& target_type = target_type_it->second;

        auto target_name_it = id_to_name.find(rec.info1);
        if (target_name_it == id_to_name.end()) continue;
        const std::string& target_name = target_name_it->second;

        if (target_type == "Group") {
            // Group-membership records come in two varieties:
            //
            //   SAP-written   (AdsDDAddUserToGroup):   prop_null = true  (plen=0xFFFF)
            //   OpenADS-written (our add_user_to_group): prop_null = false (hex property)
            //
            // SAP does NOT delete Permission records when removing a user from a
            // group — it only updates the XOR property-byte tokens (Third Pass).
            // This leaves stale records that SAP's own AdsDDGetUserProperty(1102)
            // and system.usergroups do NOT include.  Verified on pmsys.add and
            // mp.add: every SAP-written group-membership record has prop_null=true,
            // every OpenADS-written one has prop_null=false.
            //
            // Rule: trust only prop_null=false (OpenADS-written) records here.
            //       prop_null=true (SAP-written) records may be stale → skip them.
            //       Current SAP memberships are decoded from XOR tokens (Third Pass).
            if (!rec.prop_null) {
                memberships_[ci_name(principal)].insert(target_name);
            }
        } else {
            // ACL entry: store full bitmask for system.permissions.
            // SAP-written ACL records (prop_null=true, info2=0x80000000) signal
            // that the DD needs to be imported via openads_import_dd.
            if (rec.prop_null) has_sap_permissions_ = true;
            auto principal_type_it = id_to_type.find(rec.parent_id);
            bool is_grp = (principal_type_it != id_to_type.end() &&
                           principal_type_it->second == "Group");
            PermissionEntry pe;
            pe.object_name      = target_name;
            pe.object_type      = target_type;
            pe.object_type_code = type_code(target_type);
            pe.grantee          = principal;
            pe.grantee_is_group = is_grp;
            pe.bitmask          = rec.info2;
            permissions_.push_back(std::move(pe));

            // Also maintain table_perms_ for get_effective_permission().
            // SAP ADS_PERMISSION_* bit layout:
            //   bit0(0x01)=READ/SELECT  bit1(0x02)=UPDATE
            //   bit4(0x10)=INSERT       bit5(0x20)=DELETE
            //   bit31(0x80000000)=WITH_GRANT sentinel (SAP writes this for all grants)
            if (target_type == "Table") {
                int level;
                if (rec.info2 & 0x80000000u) {
                    // SAP WITH_GRANT sentinel — treat as full access (we cannot
                    // decode the actual level from the encrypted property blobs).
                    level = 4;
                } else if (rec.info2 & 0x20u) {
                    level = 3;  // DELETE present → DELETE level
                } else if (rec.info2 & 0x10u) {
                    level = 2;  // INSERT present (no DELETE) → WRITE level
                } else if (rec.info2 & 0x02u) {
                    level = 2;  // UPDATE present → WRITE level
                } else if (rec.info2 & 0x01u) {
                    level = 1;  // READ only
                } else {
                    level = 0;
                }
                table_perms_[target_name][principal] = level;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Third pass: decode User property-byte group memberships.
    //
    // Token encoding (reverse-engineered from pmsys.add with ACE32 v10,
    // verified against mp.add with SAP ACE64 v11 / 14 groups / 43 users):
    //
    //   token[slot] = K[slot] XOR group_id_LE_4bytes
    //
    //   where K[slot] is a 4-byte per-database constant.  Each user's token at
    //   slot s encodes the group that user was assigned to in s-th position.
    //   DIFFERENT users may be in different groups at the same slot index.
    //   K[slot] is the SAME for all users in the same database.
    //
    // Structure of the extra area at offset plen in the 273-byte property zone:
    //
    //   [plen+0..plen+1]  uint16 LE count_field
    //                       0xFFFF → user has no property-byte groups (skip)
    //                       N×4   → N group tokens follow
    //   [plen+2..plen+N×4+1]
    //                     N × 4-byte XOR tokens (slot 0 first)
    //   [plen+N×4+2 ..]   Post-token section (DB: built-in group membership
    //                     block).  Contains 4-byte entries alternating FFFFFFFF
    //                     separators and encoded values, optionally followed by
    //                     a uint16 length + N bytes of per-user-encrypted data.
    //                     This block uses a per-user cipher OpenADS cannot decode.
    //   [...]             0xFF padding to end of 273-byte zone
    //   [var]             XML-like <OTHER_PROPERTIES>...</OTHER_PROPERTIES> block,
    //                     stored as uint16 length + XML bytes, inside the FF zone.
    //
    //   Note: plen varies per user (it is the password-hash length), so the
    //   extra area starts at a different file offset for each user record.
    //
    // How K[slot] is derived at runtime (no documentation available):
    //
    //   K[slot] is a per-database constant computed inside the SAP ACE DLL
    //   from the 20-byte random key stored in the Database record's property
    //   field.  The derivation algorithm is proprietary and not available.
    //
    //   We brute-force K[slot] using known-plaintext: the Group obj_ids from
    //   all Group records are known, so for each candidate group_id G we
    //   compute K = first_user_token XOR G_LE and verify that every other
    //   user's slot-N token also decodes to a valid group_id.
    //
    // Disambiguation constraints:
    //
    //   1. Ascending ID order: valid group IDs are tried smallest-first.
    //      This picks the lowest-ID group for each slot's first occurrence.
    //
    //   2. No-duplicate: once a K[slot] is found, the decoded group IDs are
    //      recorded per user.  A candidate K[s] that would assign a user to a
    //      group it already holds from slot 0..s-1 is rejected.
    //
    //   3. Single-user ambiguity: if only one user has a token at slot s AND
    //      multiple K candidates pass all checks, the slot is skipped (cannot
    //      be disambiguated without a second user for cross-validation).
    //
    // Together these constraints uniquely determine K[slot] for all slots in
    // all tested databases (verified on pmsys.add, mp.add).
    //
    // Observed values for pmsys.add (pmsys database, verified with ACE32):
    //   K[0] = [0x00, 0x71, 0x0D, 0x50]  K[1] = [0xE6, 0x96, 0x26, 0x39]
    //   K[2] = [0x6F, 0xF3, 0x58, 0xE7]
    //
    // Observed values for mp.add (sfi-2021, 14 groups, 43 users, ACE64 v11):
    //   K[0] = [0xBC, 0x56, 0x29, 0x7C]  K[1] = [0xB5, 0xCF, 0x07, 0x0D]
    //   K[2] = [0xF0, 0x0B, 0x6E, 0xB2]  K[3] = [0xCF, 0xA0, 0x89, 0x50]
    //   K[4] = [0xF6, 0xCE, 0x0A, 0xD9]  K[5] = [0x8B, 0x02, 0x78, 0x96]
    //   K[6] = [0x7C, 0x9E, 0x0E, 0x6C]
    //   (7 slots used; max 8 groups/user in this DB)
    //
    // Accuracy: property-byte XOR decodes 42/43 users correctly (cross-checked
    // against system.usergroups HTML export).  The remaining user (RCB) has 3
    // additional groups in Permission records (newer-format) handled by Second
    // Pass above.  Combined accuracy: 43/43.
    //
    // Built-in groups (DB:Public, DB:Admin, DB:Debug, DB:Backup):
    //   These have NO Group records and NO XOR tokens in the .add file.
    //   DB:Public: hardcoded below — SAP spec: every authenticated user is a member.
    //
    //   DB:Admin/Backup/Debug: encoded as a per-user encrypted 16-byte block in the
    //   post-token section.  Binary layout after the last XOR token:
    //
    //     [FF FF FF FF FF FF]  6-byte separator
    //     [02 00 00 00]        marker
    //     [FF FF FF FF]        4-byte separator
    //     [02 00 00 00]        marker
    //     uint16 LE cb_len     16 → cipher block follows; 0xFFFF → absent
    //     [16 bytes]           per-user cipher block (present when cb_len==16)
    //
    //   The cipher block encodes which DB: built-in groups the user belongs to via
    //   XOR with a per-user keystream.  The full 16-byte membership encodings are
    //   known constants (confirmed universal across multiple databases):
    //     DB:Admin  delta = {D1,75,DA,BB, 00,00,00,00, 00,00,00,20, D9,3D,92,6B}
    //     DB:Backup delta = {7B,52,D9,C3, 00,00,00,00, 01,00,00,00, E0,E9,C4,79}
    //     DB:Debug  delta = {6B,D5,21,9A, 00,00,00,00, 06,00,01,00, 0A,13,0F,3F}
    //   i.e. cipher_member = keystream XOR delta; cipher_nonmember = keystream.
    //   Membership bits specifically sit at block[8:12] (Admin:{00,00,00,20},
    //   Backup:{01,00,00,00}, Debug:{06,00,01,00}).
    //
    //   WHY the cipher blocks cannot be decoded offline:
    //     The keystream is derived from a per-database key inside the SAP ACE DLL.
    //     While both LOCAL and REMOTE SAP server connections enumerate DB: group
    //     memberships correctly via SQL (system.usergroupmembers) and via
    //     AdsDDGetUserProperty(1102), the keystream itself is never exposed and no
    //     offline algorithm has been reverse-engineered.  Exhaustive cryptanalysis
    //     (DES-ECB, AES-128, TEA, XTEA, RC4 variants, differential analysis on 200
    //     consecutive OID pairs) produced no match.
    //     See tests/tools/sap_block_crack.cpp for the full analysis tool.
    //
    //   OpenADS-native persistence (current approach):
    //     add_user_to_group(user, "DB:Admin") auto-creates a Group record for the
    //     DB: built-in if none exists, then writes a Permission record.  This
    //     survives save/load without the SAP DLL.  For SAP-created .add files the
    //     cipher blocks remain undecodable; DB:Admin/Backup/Debug memberships in
    //     those files are not visible to OpenADS unless re-added explicitly.
    {
        std::unordered_set<uint32_t> valid_gids;
        for (const auto& [gid, gtype] : id_to_type)
            if (gtype == "Group") valid_gids.insert(gid);

        if (!valid_gids.empty()) {
            constexpr std::size_t MAX_SLOTS = 20;  // mp.add uses 7; pmsys uses 3
            constexpr std::size_t PROP_AREA = 273;

            struct TokEntry { std::string user; std::array<uint8_t, 4> tok; };
            std::array<std::vector<TokEntry>, MAX_SLOTS> slot_data;

            // Collect tokens from every User record.
            for (std::size_t i = 0; i < total; ++i) {
                std::size_t base = hdr_len + i * rec_len;
                if (base + rec_len > buf.size()) break;
                if (static_cast<uint8_t>(buf[base]) != 0x04) continue;
                if (trim_char(buf, base + 13, 10) != "User") continue;
                std::string uname = trim_char(buf, base + 23, 200);
                if (uname.empty()) continue;
                uint16_t plen2 = le16(buf, base + 223);
                if (plen2 == 0xFFFFu) continue;
                std::size_t xoff = base + 225 + plen2;
                std::size_t xend = base + 225 + PROP_AREA;
                if (xoff + 2 > xend) continue;
                uint16_t cf = le16(buf, xoff);
                if (cf == 0xFFFFu || cf == 0) continue;
                uint32_t ntok = static_cast<uint32_t>(cf) / 4u;
                for (uint32_t s = 0; s < ntok && s < MAX_SLOTS; ++s) {
                    std::size_t toff = xoff + 2 + s * 4;
                    if (toff + 4 > xend) break;
                    std::array<uint8_t, 4> tok = {
                        static_cast<uint8_t>(buf[toff]),
                        static_cast<uint8_t>(buf[toff + 1]),
                        static_cast<uint8_t>(buf[toff + 2]),
                        static_cast<uint8_t>(buf[toff + 3])};
                    slot_data[s].push_back({uname, tok});
                }
            }

            // Sort group IDs ascending for deterministic brute force: smaller IDs
            // are tried first, matching the natural creation order of built-in groups.
            std::vector<uint32_t> sorted_gids(valid_gids.begin(), valid_gids.end());
            std::sort(sorted_gids.begin(), sorted_gids.end());

            // For each slot, brute-force K[slot] with two constraints:
            //   (a) all tokens decode to valid group IDs, AND
            //   (b) no user is assigned to the same group more than once across slots.
            std::array<std::array<uint8_t, 4>, MAX_SLOTS> slot_keys{};
            std::array<bool, MAX_SLOTS> slot_known{};
            for (auto& k : slot_keys) k = {};
            for (auto& b : slot_known) b = false;

            // Per-user decoded group IDs (property bytes only) across slots.
            std::unordered_map<std::string, std::unordered_set<uint32_t>> user_prop_gids;

            for (std::size_t s = 0; s < MAX_SLOTS; ++s) {
                if (slot_data[s].empty()) break;
                const auto& t0 = slot_data[s][0].tok;

                // Count how many group IDs produce a valid K for this slot.
                // With multiple users any valid K is cross-validated; with a
                // single user, multiple candidates mean the result is ambiguous
                // (we cannot distinguish e.g. Readonly from Internet).
                // Strategy:
                //   n_valid == 0  → no valid K, skip
                //   n_valid == 1  → unambiguous, accept (even for 1 user)
                //   n_valid >= 2 and multiple users → cross-validated, accept first
                //   n_valid >= 2 and single user    → ambiguous, skip
                int n_valid = 0;
                std::array<uint8_t, 4> first_K{};

                for (uint32_t gid : sorted_gids) {
                    std::array<uint8_t, 4> K = {
                        static_cast<uint8_t>(t0[0] ^ static_cast<uint8_t>(gid & 0xFFu)),
                        static_cast<uint8_t>(t0[1] ^ static_cast<uint8_t>((gid >> 8) & 0xFFu)),
                        static_cast<uint8_t>(t0[2] ^ static_cast<uint8_t>((gid >> 16) & 0xFFu)),
                        static_cast<uint8_t>(t0[3] ^ static_cast<uint8_t>((gid >> 24) & 0xFFu))};
                    bool ok = true;
                    for (const auto& e : slot_data[s]) {
                        const auto& t = e.tok;
                        uint32_t dec =
                            static_cast<uint32_t>(t[0] ^ K[0]) |
                            (static_cast<uint32_t>(t[1] ^ K[1]) << 8) |
                            (static_cast<uint32_t>(t[2] ^ K[2]) << 16) |
                            (static_cast<uint32_t>(t[3] ^ K[3]) << 24);
                        if (!valid_gids.count(dec)) { ok = false; break; }
                        auto uit = user_prop_gids.find(e.user);
                        if (uit != user_prop_gids.end() && uit->second.count(dec)) {
                            ok = false; break;
                        }
                    }
                    if (ok) {
                        if (n_valid == 0) first_K = K;
                        ++n_valid;
                        // For multi-user slots the first valid K is cross-validated;
                        // no need to count further.
                        if (slot_data[s].size() >= 2) break;
                        // For single-user slots we must keep counting to detect
                        // ambiguity (more than one valid candidate).
                        if (n_valid >= 2) break;
                    }
                }

                // Skip: no valid K, or single user with multiple candidates.
                if (n_valid == 0) continue;
                if (n_valid >= 2 && slot_data[s].size() < 2) continue;

                slot_keys[s] = first_K;
                slot_known[s] = true;
                // Record decoded groups to enforce uniqueness for later slots.
                for (const auto& e : slot_data[s]) {
                    const auto& t = e.tok;
                    uint32_t dec =
                        static_cast<uint32_t>(t[0] ^ first_K[0]) |
                        (static_cast<uint32_t>(t[1] ^ first_K[1]) << 8) |
                        (static_cast<uint32_t>(t[2] ^ first_K[2]) << 16) |
                        (static_cast<uint32_t>(t[3] ^ first_K[3]) << 24);
                    user_prop_gids[e.user].insert(dec);
                }
            }

            // Decode tokens and populate memberships_.
            for (std::size_t s = 0; s < MAX_SLOTS; ++s) {
                if (!slot_known[s]) continue;
                const auto& K = slot_keys[s];
                for (const auto& e : slot_data[s]) {
                    const auto& t = e.tok;
                    uint32_t gid =
                        static_cast<uint32_t>(t[0] ^ K[0]) |
                        (static_cast<uint32_t>(t[1] ^ K[1]) << 8) |
                        (static_cast<uint32_t>(t[2] ^ K[2]) << 16) |
                        (static_cast<uint32_t>(t[3] ^ K[3]) << 24);
                    auto it = id_to_name.find(gid);
                    if (it != id_to_name.end())
                        memberships_[ci_name(e.user)].insert(it->second);
                }
            }
        }

        // DB:Public — SAP built-in group that every authenticated user belongs
        // to.  It has no Group record in the binary .add file; its membership is
        // encoded in a per-user cipher that OpenADS cannot decode.  Add it
        // unconditionally for all users so system.usergroupmembers matches SAP.
        for (const auto& uname : users_)
            memberships_[uname].insert("DB:Public");
    }

    return {};
}

util::Result<void> DataDict::load_() {
    namespace fs = std::filesystem;

    auto fres = platform::File::open(path_, platform::OpenMode::ReadOnly);
    if (!fres) {
        // Windows ERROR_SHARING_VIOLATION (sub_code 32): another process
        // has the .add file open with exclusive lock — almost certainly SAP
        // ADS/ACE.  Surface 5174 so the caller shows the import dialog.
        if (fres.error().sub_code == 32)
            return util::Error{5174, 0,
                "SAP Advantage Data Dictionary is open exclusively by another "
                "process (likely SAP ADS). Run import_dd to convert it to "
                "OpenADS format.", path_};
        return fres.error();
    }
    auto& file = fres.value();

    auto sz_res = file.size();
    if (!sz_res) return sz_res.error();
    std::uint64_t file_sz = sz_res.value();

    if (file_sz < kDdHdrLen) {
        // File too small to be our format.
        return util::Error{5103, 0,
            "DD file too small (corrupt or wrong format)", path_};
    }

    // Read header block.
    std::vector<std::uint8_t> hdr(kDdHdrLen, 0);
    if (auto r = file.read_at(0, hdr.data(), hdr.size()); !r) return r.error();

    // Check signature.
    if (std::memcmp(hdr.data(), "Advantage Table", 15) != 0) {
        // SAP binary .add files start with "ADS Data Dictionary".
        // Parse them with load_add_binary_() so the import tool can open the
        // copy it made and write memberships/permissions into it.
        // AdsConnect60 rejects binary-format DDs via has_sap_permissions().
        if (file_sz >= 19 && std::memcmp(hdr.data(), "ADS Data Dictionary", 19) == 0) {
            std::vector<std::uint8_t> raw_buf(static_cast<std::size_t>(file_sz), 0);
            if (auto r = file.read_at(0, raw_buf.data(), raw_buf.size()); !r)
                return r.error();
            std::string buf(reinterpret_cast<char*>(raw_buf.data()), raw_buf.size());
            return load_add_binary_(buf);
        }
        return util::Error{5103, 0,
            "DD file has unrecognised signature (expected OpenADS ADT format)", path_};
    }

    std::uint32_t rec_count = g32(hdr.data(), 24);
    std::uint32_t hdr_len   = g32(hdr.data(), 32);
    std::uint32_t rec_len   = g32(hdr.data(), 36);

    if (hdr_len != kDdHdrLen || rec_len != kDdRecLen) {
        return util::Error{5103, 0,
            "DD file has wrong record/header dimensions (wrong version?)", path_};
    }

    if (rec_count == 0) return {};  // empty DD — nothing to parse

    // Read all records.
    std::vector<std::uint8_t> recs(
        static_cast<std::size_t>(rec_count) * kDdRecLen, 0);
    if (auto r = file.read_at(kDdHdrLen, recs.data(), recs.size()); !r)
        return r.error();

    // Read the companion .am memo file.
    std::string am_buf;
    {
        auto amp = am_path(path_);
        auto amf = platform::File::open(amp, platform::OpenMode::ReadOnly);
        if (amf) {
            auto amsz = amf.value().size();
            if (amsz && amsz.value() > 0) {
                am_buf.assign(static_cast<std::size_t>(amsz.value()), '\0');
                if (auto r = amf.value().read_at(0, am_buf.data(), am_buf.size()); !r)
                    am_buf.clear();
            }
        }
    }

    // Helper: read JSON from .am at a given memo reference.
    auto read_json = [&](const std::uint8_t* rec_base) -> std::string {
        std::uint32_t block_no = g32(rec_base, 333);
        std::uint32_t data_len = g32(rec_base, 337);
        if (block_no == 0 || data_len == 0 || am_buf.empty()) return {};
        std::size_t off = static_cast<std::size_t>(block_no) * kAmBlock;
        if (off + data_len > am_buf.size()) return {};
        return am_buf.substr(off, data_len);
    };

    // Parse records.
    for (std::uint32_t i = 0; i < rec_count; ++i) {
        const std::uint8_t* rec = recs.data() + i * kDdRecLen;
        if (rec[0] != 0x04u) continue;  // deleted

        std::string obj_type = chf(rec + 13, 20);
        std::string obj_name = chf(rec + 33, 200);
        std::string obj_key  = chf(rec + 233, 100);
        std::string json     = read_json(rec);

        if (obj_type == "Table") {
            // OBJ_NAME=alias, OBJ_KEY=relative_path, JSON={pk,default_idx,comment,...}
            tables_[obj_name] = obj_key;
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                TableProps tp;
                if (m.count("pk"))          tp.primary_key   = m.at("pk");
                if (m.count("default_idx")) tp.default_index = m.at("default_idx");
                if (m.count("comment"))     tp.comment       = m.at("comment");
                if (m.count("user_defined"))    tp.user_defined    = m.at("user_defined");
                if (m.count("validation_expr")) tp.validation_expr = m.at("validation_expr");
                if (m.count("validation_msg"))  tp.validation_msg  = m.at("validation_msg");
                if (m.count("auto_create"))     tp.auto_create     = m.at("auto_create");
                if (m.count("memo_block_size")) tp.memo_block_size = m.at("memo_block_size");
                if (m.count("caching"))         tp.caching         = m.at("caching");
                if (m.count("encryption"))      tp.encryption      = m.at("encryption");
                if (m.count("permission_level")) tp.permission_level = m.at("permission_level");
                if (m.count("txn_free"))        tp.txn_free        = m.at("txn_free");
                if (!tp.primary_key.empty() || !tp.default_index.empty() ||
                    !tp.comment.empty() || !tp.user_defined.empty() ||
                    !tp.validation_expr.empty() || !tp.validation_msg.empty() ||
                    !tp.auto_create.empty() || !tp.memo_block_size.empty() ||
                    !tp.caching.empty() || !tp.encryption.empty() ||
                    !tp.permission_level.empty() || !tp.txn_free.empty())
                    table_props_[obj_name] = std::move(tp);
            }

        } else if (obj_type == "Index") {
            // OBJ_NAME=table_alias, OBJ_KEY=index_path,
            // JSON={comment, tag, expr, cond, opts, keylen, coll}.
            // The per-tag fields (RCB 07/16/2026) are absent in legacy DDs and
            // just come back empty.
            IndexEntry e;
            e.table_alias = obj_name;
            e.index_path  = obj_key;
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                auto get = [&](const char* k) -> std::string {
                    auto it = m.find(k);
                    return it != m.end() ? it->second : std::string();
                };
                e.comment    = get("comment");
                e.tag_name   = get("tag");
                e.expression = get("expr");
                e.condition  = get("cond");
                e.options    = get("opts");
                e.key_length = get("keylen");
                e.collation  = get("coll");
            }
            indexes_.push_back(std::move(e));

        } else if (obj_type == "User") {
            // OBJ_NAME=username, JSON={prop_*=value,...}
            const auto lo_name = ci_name(obj_name);
            // RCB 2026-06-27: Text-format OpenADS DDs also normalize user
            // records at load time, matching the binary loader and connect path.
            users_.insert(lo_name);
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                for (auto& [k, v] : m)
                    user_props_[lo_name][k] = v;
            }

        } else if (obj_type == "Group") {
            // OBJ_NAME=groupname
            groups_.insert(obj_name);

        } else if (obj_type == "Member") {
            // OBJ_NAME=username, OBJ_KEY=groupname
            // RCB 2026-06-27: Membership keys are normalized by user name so
            // effective permissions survive mixed-case imported usernames.
            memberships_[ci_name(obj_name)].insert(obj_key);

        } else if (obj_type == "Link") {
            // OBJ_NAME=alias, JSON={path,user,pwd}
            LinkEntry e;
            e.alias = obj_name;
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                if (m.count("path")) e.path = m.at("path");
                if (m.count("user")) e.user = m.at("user");
                if (m.count("pwd"))  e.pwd  = m.at("pwd");
            }
            links_[obj_name] = std::move(e);

        } else if (obj_type == "RI") {
            // OBJ_NAME=ri_name, JSON={parent,child,...}
            RiEntry e;
            e.name = obj_name;
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                if (m.count("parent"))     e.parent     = m.at("parent");
                if (m.count("child"))      e.child      = m.at("child");
                if (m.count("parent_tag")) e.parent_tag = m.at("parent_tag");
                if (m.count("child_tag"))  e.child_tag  = m.at("child_tag");
                if (m.count("upd"))        e.update_opt = m.at("upd");
                if (m.count("del"))        e.delete_opt = m.at("del");
                if (m.count("fail"))       e.fail_table = m.at("fail");
            }
            ri_[obj_name] = std::move(e);

        } else if (obj_type == "DbProp") {
            // OBJ_NAME=prop_key, JSON={value}
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                db_props_[obj_name] = m.count("value") ? m.at("value") : "";
            }

        } else if (obj_type == "FieldProp") {
            // OBJ_NAME=table, OBJ_KEY=field, JSON={key=value,...}
            if (!json.empty()) {
                auto m = json_parse_flat(json);
                for (auto& [k, v] : m)
                    field_props_[obj_name][obj_key][k] = v;
            }

        } else if (obj_type == "Perm") {
            // OBJ_NAME=obj_name, OBJ_KEY=grantee, JSON={obj_type,bitmask}
            if (!json.empty()) {
                auto m  = json_parse_flat(json);
                std::string ot = m.count("obj_type") ? m.at("obj_type") : "Table";
                std::uint32_t bitmask = 0;
                if (m.count("bitmask")) {
                    try { bitmask = static_cast<std::uint32_t>(
                              std::stoul(m.at("bitmask"))); } catch (...) {}
                }
                bool is_grp = (groups_.find(obj_key) != groups_.end());
                if (!is_grp) {
                    const auto obj_key_ci = ci_name(obj_key);
                    for (const auto& g : groups_) {
                        if (ci_name(g) == obj_key_ci) {
                            is_grp = true;
                            break;
                        }
                    }
                }
                auto type_code_of = [](const std::string& t) -> int {
                    if (t == "Table")      return 1;
                    if (t == "Column")     return 4;   // RCB 07/16/2026
                    if (t == "View")       return 6;
                    if (t == "StoredProc") return 10;
                    if (t == "Function")   return 18;
                    return 0;
                };
                PermissionEntry pe;
                pe.object_name      = obj_name;
                pe.object_type      = ot;
                pe.object_type_code = type_code_of(ot);
                pe.grantee          = obj_key;
                pe.grantee_is_group = is_grp;
                pe.bitmask          = bitmask;
                if (m.count("parent")) pe.parent = m.at("parent");
                if (pe.object_type_code == 4) has_column_perms_ = true;
                permissions_.push_back(std::move(pe));
                // Reconstruct table_perms_ coarse level from Table perms.
                if (ot == "Table") {
                    int level = 0;
                    if (bitmask & 0x80000000u)            level = 4;
                    else if (bitmask & 0x020u)            level = 3;
                    else if (bitmask & (0x010u | 0x002u)) level = 2;
                    else if (bitmask & 0x001u)            level = 1;
                    table_perms_[obj_name][obj_key] = level;
                }
            }

        } else if (obj_type == "Trigger") {
            // OBJ_NAME=table::name, JSON=trigger JSON blob
            TriggerEntry e;
            if (!json.empty()) json_to_trigger(json, e);
            if (!e.name.empty() && !e.table_alias.empty())
                triggers_[e.table_alias + "::" + e.name] = std::move(e);

        } else if (obj_type == "Proc") {
            // OBJ_NAME=proc_name, JSON=proc JSON blob
            ProcEntry e;
            if (!json.empty()) json_to_proc(json, e);
            if (!e.name.empty()) procs_[e.name] = std::move(e);

        } else if (obj_type == "Function") {
            // OBJ_NAME=func_name, JSON=function JSON blob
            FunctionEntry e;
            if (!json.empty()) json_to_func(json, e);
            if (!e.name.empty()) functions_[e.name] = std::move(e);

        } else if (obj_type == "View") {
            // OBJ_NAME=view_name, JSON=view JSON blob
            ViewEntry e;
            if (!json.empty()) json_to_view(json, e);
            if (!e.name.empty()) views_[e.name] = std::move(e);
        }
        // Unknown OBJ_TYPE values are silently skipped (forward compatibility).
    }
    return {};
}

// ---------------------------------------------------------------------------
// DataDict::save
// ---------------------------------------------------------------------------
//
// Full rewrite: serialise all in-memory data to a fresh .add + .am pair,
// then atomically replace the existing files (write to .tmp, rename).

util::Result<void> DataDict::save() {
    namespace fs = std::filesystem;

    // ---------- Collect all records to write ----------

    struct DdRec {
        std::uint32_t obj_id    = 0;
        std::uint32_t parent_id = 0;
        std::string obj_type;   // ≤20 chars
        std::string obj_name;   // ≤200 chars
        std::string obj_key;    // ≤100 chars
        std::string json;       // goes to .am memo
    };

    std::vector<DdRec> recs;
    std::uint32_t next_id = 1;

    auto mk = [&](const char* type, const std::string& name,
                  const std::string& key, const std::string& json) {
        DdRec r;
        r.obj_id   = next_id++;
        r.obj_type = type;
        r.obj_name = name;
        r.obj_key  = key;
        r.json     = json;
        recs.push_back(std::move(r));
    };

    // Tables
    {
        std::vector<std::string> keys;
        for (auto& kv : tables_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& alias : keys) {
            const std::string& path = tables_.at(alias);
            DataDict::TableProps empty_tp;
            const auto& tp = table_props_.count(alias)
                           ? table_props_.at(alias) : empty_tp;
            mk("Table", alias, path, table_to_json(tp));
        }
    }

    // Indexes. RCB 07/16/2026: one record per index TAG (SAP parity) — the
    // per-tag fields ride in the JSON, so old readers still see comment and
    // new readers pick up the rest. Multiple tags in one file share obj_key
    // (index_path); the record rows carry distinct tag/expr in JSON.
    for (auto& e : indexes_) {
        std::string j = "{\"comment\":\"" + json_escape(e.comment) + "\"";
        auto add = [&](const char* k, const std::string& v) {
            if (!v.empty()) j += ",\"" + std::string(k) + "\":\"" +
                                 json_escape(v) + "\"";
        };
        add("tag",    e.tag_name);
        add("expr",   e.expression);
        add("cond",   e.condition);
        add("opts",   e.options);
        add("keylen", e.key_length);
        add("coll",   e.collation);
        j += "}";
        mk("Index", e.table_alias, e.index_path, j);
    }

    // Users (with their properties embedded in JSON)
    {
        std::vector<std::string> us(users_.begin(), users_.end());
        std::sort(us.begin(), us.end());
        for (auto& u : us) {
            auto it = user_props_.find(u);
            std::string j = (it != user_props_.end())
                          ? user_props_to_json(it->second) : "{}";
            mk("User", u, "", j);
        }
    }

    // Groups
    {
        std::vector<std::string> gs(groups_.begin(), groups_.end());
        std::sort(gs.begin(), gs.end());
        for (auto& g : gs) mk("Group", g, "", "{}");
    }

    // Members (user → group)
    for (auto& [user, gset] : memberships_) {
        std::vector<std::string> gs(gset.begin(), gset.end());
        std::sort(gs.begin(), gs.end());
        for (auto& g : gs) mk("Member", user, g, "{}");
    }

    // Links
    {
        std::vector<std::string> keys;
        for (auto& kv : links_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& a : keys) mk("Link", a, "", link_to_json(links_.at(a)));
    }

    // RI rules
    {
        std::vector<std::string> keys;
        for (auto& kv : ri_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& n : keys) mk("RI", n, "", ri_to_json(ri_.at(n)));
    }

    // DB properties
    {
        std::vector<std::string> keys;
        for (auto& kv : db_props_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys)
            mk("DbProp", k, "",
               "{\"value\":\"" + json_escape(db_props_.at(k)) + "\"}");
    }

    // Field properties
    {
        std::vector<std::string> tbls;
        for (auto& kv : field_props_) tbls.push_back(kv.first);
        std::sort(tbls.begin(), tbls.end());
        for (auto& tbl : tbls) {
            std::vector<std::string> flds;
            for (auto& kv : field_props_.at(tbl)) flds.push_back(kv.first);
            std::sort(flds.begin(), flds.end());
            for (auto& fld : flds)
                mk("FieldProp", tbl, fld,
                   field_props_to_json(field_props_.at(tbl).at(fld)));
        }
    }

    // Permissions (fine-grained bitmask grants for all object types).
    // RCB 07/16/2026: column grants (object_type "Column") also carry a
    // parent table, written into the JSON so it survives reopen.
    for (const auto& pe : permissions_) {
        std::string j = "{\"obj_type\":\"" + json_escape(pe.object_type) + "\"";
        if (!pe.parent.empty())
            j += ",\"parent\":\"" + json_escape(pe.parent) + "\"";
        j += ",\"bitmask\":" + std::to_string(pe.bitmask) + "}";
        mk("Perm", pe.object_name, pe.grantee, j);
    }

    // Triggers
    {
        std::vector<std::string> keys;
        for (auto& kv : triggers_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys) mk("Trigger", k, "", trigger_to_json(triggers_.at(k)));
    }

    // Stored procedures
    {
        std::vector<std::string> keys;
        for (auto& kv : procs_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys) mk("Proc", k, "", proc_to_json(procs_.at(k)));
    }

    // User-defined functions
    {
        std::vector<std::string> keys;
        for (auto& kv : functions_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys) mk("Function", k, "", func_to_json(functions_.at(k)));
    }

    // Views
    {
        std::vector<std::string> keys;
        for (auto& kv : views_) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        for (auto& k : keys) mk("View", k, "", view_to_json(views_.at(k)));
    }

    // ---------- Build binary buffers ----------

    std::uint32_t rec_count = static_cast<std::uint32_t>(recs.size());

    // .add buffer: header + records
    auto add_buf = make_dd_adt_header(rec_count);
    add_buf.resize(kDdHdrLen + static_cast<std::size_t>(rec_count) * kDdRecLen, 0);

    // .am buffer: header block + data blocks
    // Block 0 is the header (next_avail at offset 20); data starts at block 1.
    std::vector<std::uint8_t> am_buf(kAmBlock, 0);
    std::uint32_t am_next = 1;

    // Write memo JSON into am_buf, return (block_no, data_len).
    auto am_append = [&](const std::string& json)
            -> std::pair<std::uint32_t, std::uint32_t> {
        if (json.empty()) return {0, 0};
        std::uint32_t start     = am_next;
        std::uint32_t data_len  = static_cast<std::uint32_t>(json.size());
        std::uint32_t blocks    = (data_len + kAmBlock - 1) / kAmBlock;
        std::size_t   old_sz    = am_buf.size();
        am_buf.resize(old_sz + static_cast<std::size_t>(blocks) * kAmBlock, 0);
        std::memcpy(am_buf.data() + old_sz, json.data(), json.size());
        am_next += blocks;
        return {start, data_len};
    };

    for (std::uint32_t i = 0; i < rec_count; ++i) {
        auto& r = recs[i];
        std::uint8_t* rec = add_buf.data() + kDdHdrLen + i * kDdRecLen;

        rec[0] = 0x04u;  // active
        p32(rec, 5, r.obj_id);
        p32(rec, 9, r.parent_id);

        // OBJ_TYPE: CHAR 20, space-padded
        std::memset(rec + 13, ' ', 20);
        std::size_t tn = std::min(r.obj_type.size(), std::size_t(20));
        if (tn) std::memcpy(rec + 13, r.obj_type.data(), tn);

        // OBJ_NAME: CHAR 200, space-padded
        std::memset(rec + 33, ' ', 200);
        std::size_t nn = std::min(r.obj_name.size(), std::size_t(200));
        if (nn) std::memcpy(rec + 33, r.obj_name.data(), nn);

        // OBJ_KEY: CHAR 100, space-padded
        std::memset(rec + 233, ' ', 100);
        std::size_t kn = std::min(r.obj_key.size(), std::size_t(100));
        if (kn) std::memcpy(rec + 233, r.obj_key.data(), kn);

        // OBJ_DATA: memo reference (block_no + data_len + 0x00)
        auto [block_no, data_len] = am_append(r.json);
        p32(rec, 333, block_no);
        p32(rec, 337, data_len);
        // rec[341] = 0x00 (already zero from resize)
    }

    // Update .am header: write next_avail at offset 20.
    p32(am_buf.data(), 20, am_next);

    // ---------- Atomic write: .add.new → .add, .am.new → .am ----------

    auto add_tmp = path_ + ".new";
    auto am_tmp  = am_path(path_) + ".new";

    {
        auto fres = platform::File::open(add_tmp, platform::OpenMode::CreateRW);
        if (!fres) return fres.error();
        if (auto r = fres.value().write_at(0, add_buf.data(), add_buf.size()); !r)
            return r.error();
        if (auto r = fres.value().sync(); !r) return r.error();
    }
    {
        auto fres = platform::File::open(am_tmp, platform::OpenMode::CreateRW);
        if (!fres) return fres.error();
        if (auto r = fres.value().write_at(0, am_buf.data(), am_buf.size()); !r)
            return r.error();
        if (auto r = fres.value().sync(); !r) return r.error();
    }

    std::error_code ec;
    fs::rename(add_tmp, path_, ec);
    if (ec) return util::Error{5000, 0,
        "DD save: rename .add.new failed: " + ec.message(), path_};
    fs::rename(am_tmp, am_path(path_), ec);
    if (ec) return util::Error{5000, 0,
        "DD save: rename .am.new failed: " + ec.message(), am_path(path_)};

    return {};
}

// ---------------------------------------------------------------------------
// TABLE
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::add_table(const std::string& alias,
                    const std::string& relative_path) {
    if (alias.empty() || relative_path.empty())
        return util::Error{5000, 0, "DD alias / path must be non-empty", ""};
    tables_[alias] = relative_path;
    return save();
}

util::Result<void> DataDict::remove_table(const std::string& alias) {
    tables_.erase(alias);
    return save();
}

std::string DataDict::resolve(const std::string& alias_or_path) const {
    auto it = tables_.find(alias_or_path);
    if (it != tables_.end()) return it->second;
    return alias_or_path;
}

std::string DataDict::get_table_property(const std::string& alias,
                                          int prop_code) const {
    auto it = table_props_.find(alias);
    if (it == table_props_.end()) return {};
    switch (prop_code) {
        case 1:   return it->second.comment;
        case 3:   return it->second.user_defined;
        case 200: return it->second.validation_expr;
        case 201: return it->second.validation_msg;
        case 202: return it->second.primary_key;
        case 203: return it->second.auto_create;
        case 213: return it->second.default_index;
        case 214: return it->second.encryption;
        case 215: return it->second.memo_block_size;
        case 216: return it->second.permission_level;
        case 217: return it->second.caching;
        case 218: return it->second.txn_free;
        case 'C': case 704: return it->second.comment;
        default:  return {};
    }
}

void DataDict::set_table_property(const std::string& alias, int prop_code,
                                   const std::string& value) {
    if (!has_alias(alias)) return;
    TableProps& tp = table_props_[alias];
    switch (prop_code) {
        case 1:   tp.comment = value; break;
        case 3:   tp.user_defined = value; break;
        case 200: tp.validation_expr = value; break;
        case 201: tp.validation_msg = value; break;
        case 202: tp.primary_key   = value; break;
        case 203: tp.auto_create   = value; break;
        case 213: tp.default_index = value; break;
        case 214: tp.encryption = value; break;
        case 215: tp.memo_block_size = value; break;
        case 216: tp.permission_level = value; break;
        case 217: tp.caching = value; break;
        case 218: tp.txn_free = value; break;
        case 'C': case 704: tp.comment = value; break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// INDEX
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::add_index_file(const std::string& table_alias,
                         const std::string& index_path,
                         const std::string& comment) {
    for (auto& e : indexes_)
        if (e.table_alias == table_alias && e.index_path == index_path)
            return save();
    IndexEntry e;
    e.table_alias = table_alias;
    e.index_path  = index_path;
    e.comment     = comment;
    indexes_.push_back(std::move(e));
    invalidate_metadata_indexes_();
    return save();
}

util::Result<void>
DataDict::add_index(const IndexEntry& in, bool persist) {
    // Replace an existing entry for the same tag (table+tag), else the same
    // file when the entry is file-only (no tag). Otherwise append.
    for (auto& e : indexes_) {
        const bool same_tag = !in.tag_name.empty() &&
            e.table_alias == in.table_alias && e.tag_name == in.tag_name;
        const bool same_file = in.tag_name.empty() &&
            e.table_alias == in.table_alias && e.index_path == in.index_path;
        if (same_tag || same_file) {
            e = in;
            invalidate_metadata_indexes_();
            return persist ? save() : util::Result<void>{};
        }
    }
    indexes_.push_back(in);
    invalidate_metadata_indexes_();
    return persist ? save() : util::Result<void>{};
}

util::Result<void>
DataDict::clear_indexes(bool persist) {
    indexes_.clear();
    invalidate_metadata_indexes_();
    return persist ? save() : util::Result<void>{};
}

util::Result<void>
DataDict::remove_index_file(const std::string& table_alias,
                             const std::string& index_path) {
    indexes_.erase(
        std::remove_if(indexes_.begin(), indexes_.end(),
            [&](const IndexEntry& e) {
                return e.table_alias == table_alias &&
                       e.index_path  == index_path;
            }),
        indexes_.end());
    invalidate_metadata_indexes_();
    return save();
}

// ---------------------------------------------------------------------------
// USER / GROUP / MEMBER
// ---------------------------------------------------------------------------

// RCB 2026-06-27: ADS DD user names are case-insensitive. Fold to
// lowercase before storage or lookup so "ADSSYS", "adssys", and "AdsSys"
// resolve to the same user across imports, login, and permission checks.
/* static */ std::string DataDict::ci_name(const std::string& s) noexcept {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool DataDict::has_user(const std::string& user) const noexcept {
    return users_.find(ci_name(user)) != users_.end();
}

util::Result<void> DataDict::create_user(const std::string& user) {
    if (user.empty())
        return util::Error{5000, 0, "DD user name empty", ""};
    users_.insert(ci_name(user));
    return save();
}

util::Result<void> DataDict::delete_user(const std::string& user) {
    const auto lo = ci_name(user);
    if (lo == "adssys")
        return util::Error{5000, 0, "AdsSys user cannot be deleted", ""};
    users_.erase(lo);
    memberships_.erase(lo);
    user_props_.erase(lo);
    invalidate_metadata_indexes_();
    return save();
}

util::Result<void>
DataDict::add_user_to_group(const std::string& user,
                            const std::string& group) {
    if (user.empty() || group.empty())
        return util::Error{5000, 0, "DD member user / group empty", ""};
    // RCB 2026-06-28: SAP dictionaries expect the built-in AdsSys account to
    // belong to DB:Admin.  Keep AdsSys locked down for arbitrary groups, but
    // allow import and round-trip code to persist the required admin role.
    if (ci_name(user) == "adssys" && ci_name(group) != "db:admin")
        return util::Error{5000, 0, "AdsSys group membership cannot be changed", ""};
    // Auto-create DB: built-in groups in groups_ so they round-trip.
    if (group.size() >= 3 &&
        std::toupper(static_cast<unsigned char>(group[0])) == 'D' &&
        std::toupper(static_cast<unsigned char>(group[1])) == 'B' &&
        group[2] == ':')
        groups_.insert(group);
    memberships_[ci_name(user)].insert(group);
    invalidate_metadata_indexes_();
    return save();
}

util::Result<void>
DataDict::remove_user_from_group(const std::string& user,
                                 const std::string& group) {
    if (ci_name(user) == "adssys")
        return util::Error{5000, 0, "AdsSys group membership cannot be changed", ""};
    auto it = memberships_.find(ci_name(user));
    if (it != memberships_.end()) {
        it->second.erase(group);
        if (it->second.empty()) memberships_.erase(it);
    }
    invalidate_metadata_indexes_();
    return save();
}

bool DataDict::is_member_of(const std::string& user,
                            const std::string& group) const {
    // RCB 2026-06-28: Treat AdsSys as DB:Admin even when older DD metadata did
    // not persist that membership, matching SAP DD built-in behavior.
    if (ci_name(user) == "adssys" && ci_name(group) == "db:admin")
        return true;
    auto it = memberships_.find(ci_name(user));
    if (it == memberships_.end()) return false;
    return it->second.find(group) != it->second.end();
}

const std::unordered_set<std::string>&
DataDict::groups_of(const std::string& user) const {
    static const std::unordered_set<std::string> empty;
    auto it = memberships_.find(ci_name(user));
    return it == memberships_.end() ? empty : it->second;
}

util::Result<void> DataDict::create_group(const std::string& group) {
    if (group.empty())
        return util::Error{5000, 0, "DD group name empty", ""};
    groups_.insert(group);
    return save();
}

util::Result<void> DataDict::delete_group(const std::string& group) {
    groups_.erase(group);
    // Remove all memberships referencing this group.
    for (auto& [u, gs] : memberships_) gs.erase(group);
    invalidate_metadata_indexes_();
    return save();
}

// ---------------------------------------------------------------------------
// LINK
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::create_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    if (alias.empty() || path.empty())
        return util::Error{5000, 0, "DD link alias / path empty", ""};
    links_[alias] = {alias, path, user, pwd};
    return save();
}

util::Result<void> DataDict::drop_link(const std::string& alias) {
    links_.erase(alias);
    return save();
}

util::Result<void>
DataDict::modify_link(const std::string& alias, const std::string& path,
                      const std::string& user, const std::string& pwd) {
    auto it = links_.find(alias);
    if (it == links_.end())
        return util::Error{5000, 0, "DD link not found", alias};
    if (!path.empty()) it->second.path = path;
    it->second.user = user;
    it->second.pwd  = pwd;
    return save();
}

// ---------------------------------------------------------------------------
// REFERENTIAL INTEGRITY
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_ri(const RiEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD RI name empty", ""};
    ri_[e.name] = e;
    invalidate_metadata_indexes_();
    return save();
}

util::Result<void> DataDict::remove_ri(const std::string& name) {
    ri_.erase(name);
    invalidate_metadata_indexes_();
    return save();
}

// ---------------------------------------------------------------------------
// DB / USER PROPERTIES
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::set_db_property(const std::string& key, const std::string& value) {
    if (key.empty())
        return util::Error{5000, 0, "DD db-property key empty", ""};
    db_props_[key] = value;
    return save();
}

std::string DataDict::get_db_property(const std::string& key) const {
    auto it = db_props_.find(key);
    return it == db_props_.end() ? std::string{} : it->second;
}

util::Result<void>
DataDict::set_user_property(const std::string& user,
                            const std::string& key,
                            const std::string& value) {
    if (user.empty() || key.empty())
        return util::Error{5000, 0, "DD user-property user / key empty", ""};
    user_props_[ci_name(user)][key] = value;
    return save();
}

std::string
DataDict::get_user_property(const std::string& user,
                            const std::string& key) const {
    auto u = user_props_.find(ci_name(user));
    if (u == user_props_.end()) return {};
    auto k = u->second.find(key);
    return k == u->second.end() ? std::string{} : k->second;
}

// ---------------------------------------------------------------------------
// PERMISSIONS
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::grant_permission(const std::string& obj_type,
                            const std::string& obj_name,
                            const std::string& grantee,
                            uint32_t bitmask) {
    if (obj_type.empty() || obj_name.empty() || grantee.empty())
        return util::Error{5000, 0, "PERM obj_type/obj_name/grantee empty", ""};

    bool is_grp = (groups_.find(grantee) != groups_.end());
    if (!is_grp) {
        const auto grantee_ci = ci_name(grantee);
        for (const auto& g : groups_) {
            if (ci_name(g) == grantee_ci) {
                is_grp = true;
                break;
            }
        }
    }

    // Keep table_perms_ coarse-level map in sync for Table objects.
    if (obj_type == "Table") {
        int lvl = 0;
        if (bitmask & 0x80000000u)            lvl = 4;
        else if (bitmask & 0x020u)            lvl = 3;
        else if (bitmask & (0x010u | 0x002u)) lvl = 2;
        else if (bitmask & 0x001u)            lvl = 1;
        table_perms_[obj_name][grantee] = lvl;
    }

    invalidate_perm_cache_();

    // Update existing permissions_ entry or append a new one.
    for (auto& pe : permissions_) {
        if (pe.object_name == obj_name && pe.object_type == obj_type &&
            pe.grantee == grantee) {
            pe.bitmask          = bitmask;
            pe.grantee_is_group = is_grp;
            return save();
        }
    }
    auto type_code_of = [](const std::string& t) -> int {
        if (t == "Table")      return 1;
        if (t == "Field")      return 4;
        if (t == "View")       return 6;
        if (t == "User")       return 8;
        if (t == "Group")      return 9;
        if (t == "StoredProc") return 10;
        if (t == "Database")   return 11;
        if (t == "Link")       return 12;
        if (t == "Function")   return 18;
        return 0;
    };
    PermissionEntry pe;
    pe.object_name      = obj_name;
    pe.object_type      = obj_type;
    pe.object_type_code = type_code_of(obj_type);
    pe.grantee          = grantee;
    pe.grantee_is_group = is_grp;
    pe.bitmask          = bitmask;
    permissions_.push_back(std::move(pe));
    return save();
}

util::Result<void>
DataDict::grant_column_permission(const std::string& table,
                                  const std::string& column,
                                  const std::string& grantee,
                                  uint32_t bitmask, bool persist) {
    if (table.empty() || column.empty() || grantee.empty())
        return util::Error{5000, 0,
                           "column perm: table/column/grantee empty", ""};
    bool is_grp = groups_.count(grantee) != 0;
    if (!is_grp) {
        const auto gci = ci_name(grantee);
        for (const auto& g : groups_)
            if (ci_name(g) == gci) { is_grp = true; break; }
    }
    // Replace an existing (table, column, grantee) grant, else append.
    for (auto& pe : permissions_) {
        if (pe.object_type_code == 4 &&
            ci_name(pe.parent) == ci_name(table) &&
            ci_name(pe.object_name) == ci_name(column) &&
            ci_name(pe.grantee) == ci_name(grantee)) {
            pe.bitmask = bitmask;
            has_column_perms_ = true;
            invalidate_perm_cache_();
            return persist ? save() : util::Result<void>{};
        }
    }
    PermissionEntry pe;
    pe.object_name      = column;
    pe.object_type      = "Column";
    pe.object_type_code = 4;
    pe.grantee          = grantee;
    pe.grantee_is_group = is_grp;
    pe.bitmask          = bitmask;
    pe.parent           = table;
    permissions_.push_back(std::move(pe));
    has_column_perms_ = true;
    invalidate_perm_cache_();
    return persist ? save() : util::Result<void>{};
}

std::optional<std::unordered_set<std::string>>
DataDict::permitted_columns(const std::string& username,
                            const std::string& table,
                            uint32_t op_bit) const {
    // Fast opt-out: no column security anywhere, or an admin principal.
    if (!has_column_perms_) return std::nullopt;
    const auto lo = ci_name(username);
    if (lo == "adssys") return std::nullopt;
    auto mg = memberships_.find(lo);
    // DB:Admin members are unrestricted (inline ci check — has_ci_group is a
    // file-local static defined later in this TU).
    if (mg != memberships_.end()) {
        for (const auto& g : mg->second)
            if (ci_name(g) == "db:admin") return std::nullopt;
    }

    // Principals: the user + every group they belong to (both raw + folded).
    std::unordered_set<std::string> principals;
    principals.insert(lo);
    if (mg != memberships_.end())
        for (const auto& g : mg->second) {
            principals.insert(g);
            principals.insert(ci_name(g));
        }

    const auto tci = ci_name(table);
    std::unordered_set<std::string> cols;
    bool restricted = false;
    for (const auto& pe : permissions_) {
        if (pe.object_type_code != 4) continue;          // column grants only
        if (ci_name(pe.parent) != tci) continue;         // this table
        if (principals.find(pe.grantee) == principals.end() &&
            principals.find(ci_name(pe.grantee)) == principals.end())
            continue;                                    // not one of my grants
        // Per-op: a column grant restricts an op only when it carries that
        // op's bit. So SELECT is column-restricted iff a column-SELECT grant
        // exists; a row-level op like DELETE (never granted per column) stays
        // table-level. This is what closes the SELECT-exposure hole precisely.
        if (pe.bitmask & op_bit) {
            restricted = true;
            cols.insert(ci_name(pe.object_name));
        }
    }
    if (!restricted) return std::nullopt;   // table-level applies to all columns
    return cols;                            // ONLY these columns for this op
}

util::Result<void>
DataDict::set_table_permission(const std::string& table,
                                const std::string& user_or_group,
                                int level) {
    if (table.empty() || user_or_group.empty())
        return util::Error{5000, 0, "TABLEPERM table/user empty", ""};
    std::uint32_t bitmask = 0;
    if (level >= 1) bitmask |= DD_PERM_SELECT;
    if (level >= 2) bitmask |= DD_PERM_UPDATE | DD_PERM_INSERT;
    if (level >= 3) bitmask |= DD_PERM_DELETE;
    if (level >= 4) bitmask  = DD_PERM_FULL;
    // grant_permission() will call invalidate_perm_cache_() itself.
    return grant_permission("Table", table, user_or_group, bitmask);
}

// ---------------------------------------------------------------------------
// Effective permission helpers
// ---------------------------------------------------------------------------

namespace {

// Merge a raw bitmask from one PermissionEntry into an accumulated result.
// The FULL sentinel (bit 31) grants all DML bits.
static uint32_t expand_bitmask(uint32_t mask) {
    if (mask & DataDict::DD_PERM_FULL)
        return DataDict::DD_PERM_SELECT | DataDict::DD_PERM_UPDATE |
               DataDict::DD_PERM_INSERT | DataDict::DD_PERM_DELETE |
               DataDict::DD_PERM_EXECUTE | DataDict::DD_PERM_REFERENCE |
               DataDict::DD_PERM_GRANT;
    return mask;
}

static DataDict::EffectiveOps ops_from_bitmask(uint32_t mask) {
    uint32_t m = expand_bitmask(mask);
    DataDict::EffectiveOps ops;
    ops.select_  = (m & DataDict::DD_PERM_SELECT)  != 0;
    ops.update_  = (m & DataDict::DD_PERM_UPDATE)  != 0;
    ops.execute_ = (m & DataDict::DD_PERM_EXECUTE) != 0;
    ops.insert_  = (m & DataDict::DD_PERM_INSERT)  != 0;
    ops.delete_  = (m & DataDict::DD_PERM_DELETE)  != 0;
    return ops;
}

static bool has_ci_group(const std::unordered_set<std::string>& groups,
                         const char* wanted) {
    for (const auto& g : groups) {
        if (DataDict::ci_name(g) == wanted) return true;
    }
    return false;
}

} // namespace

bool DataDict::has_acl_for_object(const std::string& object_name) const {
    return !permissions_by_object(object_name).empty();
}

std::string DataDict::permission_key_(const std::string& grantee,
                                      const std::string& obj_name,
                                      int object_type_code) {
    return ci_name(grantee) + '\x1f' + ci_name(obj_name) + '\x1f' +
           std::to_string(object_type_code);
}

void DataDict::build_permission_indexes_() const {
    if (perm_indexes_valid_) return;
    permissions_by_grantee_ci_.clear();
    permissions_by_object_ci_.clear();
    permissions_by_grantee_object_.clear();
    permissions_by_grantee_ci_.reserve(permissions_.size());
    permissions_by_object_ci_.reserve(permissions_.size());
    permissions_by_grantee_object_.reserve(permissions_.size());

    // RCB 06/30/2026: Build DD permission indexes in core so local and remote
    // clients share one fast metadata path instead of each UI scanning the full
    // system.permissions compatibility surface independently.
    for (const auto& pe : permissions_) {
        permissions_by_grantee_ci_[ci_name(pe.grantee)].push_back(&pe);
        permissions_by_object_ci_[ci_name(pe.object_name)].push_back(&pe);
        permissions_by_grantee_object_.emplace(
            permission_key_(pe.grantee, pe.object_name, pe.object_type_code),
            &pe);
    }
    perm_indexes_valid_ = true;
}

const std::vector<const DataDict::PermissionEntry*>&
DataDict::permissions_by_grantee(const std::string& grantee) const {
    build_permission_indexes_();
    static const std::vector<const PermissionEntry*> kEmpty;
    auto it = permissions_by_grantee_ci_.find(ci_name(grantee));
    return it == permissions_by_grantee_ci_.end() ? kEmpty : it->second;
}

const std::vector<const DataDict::PermissionEntry*>&
DataDict::permissions_by_object(const std::string& object_name) const {
    build_permission_indexes_();
    static const std::vector<const PermissionEntry*> kEmpty;
    auto it = permissions_by_object_ci_.find(ci_name(object_name));
    return it == permissions_by_object_ci_.end() ? kEmpty : it->second;
}

const DataDict::PermissionEntry*
DataDict::find_permission(const std::string& grantee,
                          const std::string& obj_name,
                          int object_type_code) const {
    build_permission_indexes_();
    auto it = permissions_by_grantee_object_.find(
        permission_key_(grantee, obj_name, object_type_code));
    return it == permissions_by_grantee_object_.end() ? nullptr : it->second;
}

uint32_t DataDict::get_permission_mask(const std::string& grantee,
                                       const std::string& obj_name,
                                       int object_type_code,
                                       bool include_inherited) const {
    uint32_t mask = 0;
    if (const auto* pe = find_permission(grantee, obj_name, object_type_code))
        mask |= pe->bitmask;

    if (!include_inherited) return mask;

    const auto grantee_ci = ci_name(grantee);
    if (groups_.find(grantee) != groups_.end()) return mask;
    for (const auto& g : groups_) {
        if (ci_name(g) == grantee_ci) return mask;
    }

    auto mg = memberships_.find(grantee_ci);
    if (mg == memberships_.end()) return mask;
    for (const auto& group : mg->second) {
        if (const auto* pe = find_permission(group, obj_name, object_type_code))
            mask |= pe->bitmask;
    }
    return mask;
}

std::string DataDict::trigger_event_key_(const std::string& table_alias,
                                         std::uint32_t event_mask,
                                         std::uint32_t timing) {
    return ci_name(table_alias) + '\x1f' + std::to_string(event_mask) + '\x1f' +
           std::to_string(timing);
}

void DataDict::build_metadata_indexes_() const {
    if (metadata_indexes_valid_) return;
    indexes_by_table_ci_.clear();
    users_by_group_ci_.clear();
    ri_by_parent_table_ci_.clear();
    ri_by_child_table_ci_.clear();
    triggers_by_table_ci_.clear();
    triggers_by_event_ci_.clear();
    indexes_by_table_ci_.reserve(indexes_.size());
    users_by_group_ci_.reserve(memberships_.size());
    ri_by_parent_table_ci_.reserve(ri_.size());
    ri_by_child_table_ci_.reserve(ri_.size());
    triggers_by_table_ci_.reserve(triggers_.size());
    triggers_by_event_ci_.reserve(triggers_.size());

    // RCB 06/30/2026: These secondary DD indexes are intentionally small and
    // lazy. They avoid scanning all metadata on common client queries while
    // keeping DD load and save behavior simple.
    for (const auto& e : indexes_)
        indexes_by_table_ci_[ci_name(e.table_alias)].push_back(&e);

    for (const auto& [user, groups] : memberships_) {
        for (const auto& group : groups)
            users_by_group_ci_[ci_name(group)].push_back(user);
    }
    for (auto& [group, users] : users_by_group_ci_)
        std::sort(users.begin(), users.end());

    for (const auto& [name, rule] : ri_) {
        (void)name;
        ri_by_parent_table_ci_[ci_name(rule.parent)].push_back(&rule);
        ri_by_child_table_ci_[ci_name(rule.child)].push_back(&rule);
    }

    for (const auto& [name, trig] : triggers_) {
        (void)name;
        triggers_by_table_ci_[ci_name(trig.table_alias)].push_back(&trig);
        if (trig.enabled) {
            triggers_by_event_ci_[trigger_event_key_(
                trig.table_alias, trig.event_mask, trig.timing)].push_back(&trig);
        }
    }
    auto by_priority = [](const TriggerEntry* a, const TriggerEntry* b) {
        return a->priority < b->priority;
    };
    for (auto& [table, list] : triggers_by_table_ci_) {
        (void)table;
        std::sort(list.begin(), list.end(), by_priority);
    }
    for (auto& [event_key, list] : triggers_by_event_ci_) {
        (void)event_key;
        std::sort(list.begin(), list.end(), by_priority);
    }

    metadata_indexes_valid_ = true;
}

const std::vector<const DataDict::IndexEntry*>&
DataDict::indexes_for_table(const std::string& table_alias) const {
    build_metadata_indexes_();
    static const std::vector<const IndexEntry*> kEmpty;
    auto it = indexes_by_table_ci_.find(ci_name(table_alias));
    return it == indexes_by_table_ci_.end() ? kEmpty : it->second;
}

const std::vector<std::string>&
DataDict::users_in_group(const std::string& group) const {
    build_metadata_indexes_();
    static const std::vector<std::string> kEmpty;
    auto it = users_by_group_ci_.find(ci_name(group));
    return it == users_by_group_ci_.end() ? kEmpty : it->second;
}

const std::vector<const DataDict::RiEntry*>&
DataDict::ri_by_parent_table(const std::string& table_alias) const {
    build_metadata_indexes_();
    static const std::vector<const RiEntry*> kEmpty;
    auto it = ri_by_parent_table_ci_.find(ci_name(table_alias));
    return it == ri_by_parent_table_ci_.end() ? kEmpty : it->second;
}

const std::vector<const DataDict::RiEntry*>&
DataDict::ri_by_child_table(const std::string& table_alias) const {
    build_metadata_indexes_();
    static const std::vector<const RiEntry*> kEmpty;
    auto it = ri_by_child_table_ci_.find(ci_name(table_alias));
    return it == ri_by_child_table_ci_.end() ? kEmpty : it->second;
}

const std::vector<const DataDict::TriggerEntry*>&
DataDict::triggers_for_table(const std::string& table_alias) const {
    build_metadata_indexes_();
    static const std::vector<const TriggerEntry*> kEmpty;
    auto it = triggers_by_table_ci_.find(ci_name(table_alias));
    return it == triggers_by_table_ci_.end() ? kEmpty : it->second;
}

const std::vector<const DataDict::TriggerEntry*>&
DataDict::triggers_for_event(const std::string& table_alias,
                             std::uint32_t event_mask,
                             std::uint32_t timing) const {
    build_metadata_indexes_();
    static const std::vector<const TriggerEntry*> kEmpty;
    auto it = triggers_by_event_ci_.find(
        trigger_event_key_(table_alias, event_mask, timing));
    return it == triggers_by_event_ci_.end() ? kEmpty : it->second;
}

// Build the effective-permission cache for the named user.
// For each distinct (object_name) that appears in permissions_,
// we OR together the bits from direct user grants and every group
// the user belongs to.
void DataDict::build_perm_cache(const std::string& username) const {
    const auto lo = ci_name(username);
    // RCB 2026-06-27: Store the normalized user plus both original and
    // folded group names. Imported SAP DDs commonly mix group casing
    // (for example General/general or DB:Admin/db:Admin), but effective
    // permissions must still match the memberships shown for the user.
    // Collect the user's principals (self + groups).
    std::unordered_set<std::string> principals;
    principals.insert(lo);
    auto mg = memberships_.find(lo);
    if (mg != memberships_.end()) {
        for (const auto& g : mg->second) {
            principals.insert(g);
            principals.insert(ci_name(g));
        }
    }
    if (lo == "adssys") {
        principals.insert("DB:Admin");
        principals.insert("db:admin");
    }

    // Store the raw OR of all bitmasks for each object.  Do NOT expand FULL
    // here — we need the sentinel bit preserved so get_effective_permission()
    // can distinguish level 4 from level 3.  Expansion happens at check time.
    std::unordered_map<std::string, uint32_t> obj_bits;
    std::unordered_set<const PermissionEntry*> seen;
    for (const auto& principal : principals) {
        for (const auto* pe : permissions_by_grantee(principal)) {
            if (seen.insert(pe).second)
                obj_bits[pe->object_name] |= pe->bitmask;
        }
    }
    perm_cache_[lo] = std::move(obj_bits);
}

bool DataDict::check_perm(const std::string& username,
                           const std::string& object_name,
                           uint32_t           required) const {
    // No ACL at all → open access.
    if (permissions_.empty()) return true;

    const auto lo = ci_name(username);
    // RCB 2026-06-28: DB:Admin and AdsSys must remain administrative bypasses;
    // regular users with no effective grant are denied below when an object has
    // explicit DD ACL rows for someone else.
    if (lo == "adssys") return true;
    auto mg = memberships_.find(lo);
    if (mg != memberships_.end() && has_ci_group(mg->second, "db:admin"))
        return true;

    // Ensure cache is populated for this user.
    if (perm_cache_.find(lo) == perm_cache_.end())
        build_perm_cache(lo);

    const auto& user_map = perm_cache_.at(lo);
    auto it = user_map.find(object_name);
    if (it == user_map.end())
        return !has_acl_for_object(object_name);

    // Expand raw bitmask before testing (FULL sentinel → all individual bits).
    return (expand_bitmask(it->second) & required) == required;
}

DataDict::EffectiveOps DataDict::get_effective_ops(
        const std::string& username,
        const std::string& object_name) const {
    // No ACL defined at all → all ops permitted.
    if (permissions_.empty()) {
        EffectiveOps full;
        full.open = full.select_ = full.update_ =
            full.insert_ = full.delete_ = full.execute_ = true;
        return full;
    }

    const auto lo = ci_name(username);
    if (lo == "adssys") {
        EffectiveOps full;
        full.open = full.select_ = full.update_ =
            full.insert_ = full.delete_ = full.execute_ = true;
        return full;
    }
    auto mg = memberships_.find(lo);
    if (mg != memberships_.end() && has_ci_group(mg->second, "db:admin")) {
        EffectiveOps full;
        full.open = full.select_ = full.update_ =
            full.insert_ = full.delete_ = full.execute_ = true;
        return full;
    }

    // Use cache (build lazily if needed).
    if (perm_cache_.find(lo) == perm_cache_.end())
        build_perm_cache(lo);

    const auto& user_map = perm_cache_.at(lo);
    auto it = user_map.find(object_name);
    if (it == user_map.end()) {
        // RCB 2026-06-28: Preserve the historical "no ACL exists for this
        // object" open behavior, but deny users who have no effective grant
        // for an object that is explicitly protected by DD permissions.
        EffectiveOps full;
        if (has_acl_for_object(object_name)) return full;
        full.open = full.select_ = full.update_ =
            full.insert_ = full.delete_ = full.execute_ = true;
        return full;
    }

    return ops_from_bitmask(it->second);
}

std::vector<DataDict::EffectivePermEntry>
DataDict::get_all_effective_perms(const std::string& username) const {
    const auto lo = ci_name(username);
    // Ensure cache is built.
    if (perm_cache_.find(lo) == perm_cache_.end())
        build_perm_cache(lo);

    // Build object_type lookup from the raw permission entries.
    std::unordered_map<std::string, std::string> obj_type_map;
    std::unordered_map<std::string, int>          obj_code_map;
    std::unordered_set<const PermissionEntry*> seen_entries;
    std::unordered_set<std::string> principals{lo};
    auto mg = memberships_.find(lo);
    if (mg != memberships_.end()) {
        for (const auto& g : mg->second) {
            principals.insert(g);
            principals.insert(ci_name(g));
        }
    }
    for (const auto& principal : principals) {
        for (const auto* pe : permissions_by_grantee(principal)) {
            if (!seen_entries.insert(pe).second) continue;
            obj_type_map[pe->object_name] = pe->object_type;
            obj_code_map[pe->object_name] = pe->object_type_code;
        }
    }

    const auto& user_map = perm_cache_.at(lo);
    std::vector<EffectivePermEntry> result;
    result.reserve(user_map.size());
    for (const auto& kv : user_map) {
        EffectivePermEntry e;
        e.object_name      = kv.first;
        e.object_type      = obj_type_map[kv.first];
        e.object_type_code = obj_code_map[kv.first];
        e.grantee          = lo;
        e.ops              = ops_from_bitmask(kv.second);
        result.push_back(std::move(e));
    }
    return result;
}

int DataDict::get_effective_permission(const std::string& username,
                                        const std::string& table) const {
    const auto lo = ci_name(username);
    if (lo == "adssys") return 4;
    auto mg = memberships_.find(lo);
    if (mg != memberships_.end() && has_ci_group(mg->second, "db:admin"))
        return 4;

    // Prefer the fine-grained cache when available; fall back to table_perms_.
    if (!permissions_.empty()) {
        if (perm_cache_.find(lo) == perm_cache_.end())
            build_perm_cache(lo);
        const auto& user_map = perm_cache_.at(lo);
        auto it = user_map.find(table);
        // RCB 2026-06-28: A missing effective row means level 0 only when the
        // table has ACL rows for someone else; otherwise keep legacy open DDs
        // at level 4 so older dictionaries without ACLs remain usable.
        if (it == user_map.end()) return has_acl_for_object(table) ? 0 : 4;
        uint32_t bits = it->second;
        if (bits & DD_PERM_FULL)                                   return 4;
        if (bits & DD_PERM_DELETE)                                 return 3;
        if (bits & (DD_PERM_UPDATE | DD_PERM_INSERT))              return 2;
        if (bits & DD_PERM_SELECT)                                 return 1;
        return 0;
    }

    // Legacy fallback: coarse table_perms_ map.
    auto t = table_perms_.find(table);
    if (t == table_perms_.end()) return 4;
    int eff = 0;
    auto u = t->second.find(lo);
    if (u != t->second.end()) eff = u->second;
    auto legacy_mg = memberships_.find(lo);
    if (legacy_mg != memberships_.end()) {
        for (const auto& g : legacy_mg->second) {
            auto gi = t->second.find(g);
            if (gi != t->second.end() && gi->second > eff)
                eff = gi->second;
        }
    }
    return eff;
}

// ---------------------------------------------------------------------------
// FIELD PROPERTIES
// ---------------------------------------------------------------------------

util::Result<void>
DataDict::set_field_property(const std::string& table,
                              const std::string& field,
                              const std::string& key,
                              const std::string& value) {
    if (table.empty() || field.empty() || key.empty())
        return util::Error{5000, 0, "DD field-property table/field/key empty", ""};
    field_props_[table][field][key] = value;
    return save();
}

std::string
DataDict::get_field_property(const std::string& table,
                              const std::string& field,
                              const std::string& key) const {
    auto t = field_props_.find(table);
    if (t == field_props_.end()) return {};
    auto f = t->second.find(field);
    if (f == t->second.end()) return {};
    auto k = f->second.find(key);
    return k == f->second.end() ? std::string{} : k->second;
}

// ---------------------------------------------------------------------------
// TRIGGERS
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_trigger(const TriggerEntry& e) {
    if (e.name.empty() || e.table_alias.empty())
        return util::Error{5000, 0, "DD trigger name/table empty", ""};
    triggers_[e.table_alias + "::" + e.name] = e;
    invalidate_metadata_indexes_();
    return save();
}

util::Result<void> DataDict::drop_trigger(const std::string& name) {
    auto sep = name.find("::");
    std::string erase_key = name;
    if (sep == std::string::npos) {
        const auto* ep = find_trigger(name);
        if (ep) erase_key = ep->table_alias + "::" + ep->name;
    }
    triggers_.erase(erase_key);
    invalidate_metadata_indexes_();
    return save();
}

// ---------------------------------------------------------------------------
// STORED PROCEDURES
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_proc(const ProcEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD proc name empty", ""};
    procs_[e.name] = e;
    return save();
}

util::Result<void> DataDict::drop_proc(const std::string& name) {
    procs_.erase(name);
    return save();
}

// ---------------------------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_function(const FunctionEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD function name empty", ""};
    functions_[e.name] = e;
    return save();
}

util::Result<void> DataDict::drop_function(const std::string& name) {
    functions_.erase(name);
    return save();
}

// ---------------------------------------------------------------------------
// VIEWS
// ---------------------------------------------------------------------------

util::Result<void> DataDict::create_view(const ViewEntry& e) {
    if (e.name.empty())
        return util::Error{5000, 0, "DD view name empty", ""};
    views_[e.name] = e;
    return save();
}

util::Result<void> DataDict::drop_view(const std::string& name) {
    views_.erase(name);
    return save();
}

} // namespace openads::engine
