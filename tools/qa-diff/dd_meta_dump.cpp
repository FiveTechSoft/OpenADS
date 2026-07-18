// dd_meta_dump — read-only Data-Dictionary metadata dumper for parity testing.
//
// Dynamically loads an ACE-compatible DLL (SAP ace64.dll or OpenADS
// openace64.dll), connects to a .add dictionary, runs a battery of `system.*`
// catalog queries, and emits a NORMALIZED JSON document to stdout. Run it once
// per engine against the matching .add and diff the two JSON files: SAP is the
// oracle, so any divergence is a candidate OpenADS bug.
//
//   dd_meta_dump --lib <ace64|openace64>.dll --db <path.add>
//                --user <name> [--password <pw>]
//                [--only tables,columns,indexes]   (default: all sections)
//
// Read-only by construction: it only uses the dynamically-loaded Ads* SQL API,
// so it links nothing (no openads_core) and builds with a plain `cl.exe`.
//
// Design notes:
//  * Every section query is run independently; a query that FAILS on one
//    engine but succeeds on the other is itself a finding, so a failure is
//    emitted as {"error":"rc=N"} rather than aborting the dump.
//  * Rows are ORDER BY'd in SQL and field values are right-trimmed, so a plain
//    textual diff of two dumps is meaningful. Field names are emitted verbatim
//    (SAP's own casing); if the two engines disagree on column casing that is a
//    finding we WANT to see.

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Dynamic library loader (same pattern as tools/import_dd)
// ---------------------------------------------------------------------------
#ifdef _WIN32
using lib_handle = HMODULE;
static lib_handle lib_open(const char* path) {
    std::string p = path;
    auto sep = p.find_last_of("\\/");
    if (sep != std::string::npos) SetDllDirectoryA(p.substr(0, sep).c_str());
    HMODULE h = LoadLibraryA(path);
    SetDllDirectoryA(nullptr);
    return h;
}
static void* lib_sym(lib_handle h, const char* n) {
    return reinterpret_cast<void*>(GetProcAddress(h, n));
}
#else
using lib_handle = void*;
static lib_handle lib_open(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void*      lib_sym(lib_handle h, const char* n) { return dlsym(h, n); }
#endif

using UNSIGNED8  = unsigned char;
using UNSIGNED16 = unsigned short;
using UNSIGNED32 = unsigned int;
using SIGNED32   = int;
using ADSHANDLE  = unsigned long long;

#ifdef _WIN32
#  define ADS_CALL __stdcall
#else
#  define ADS_CALL
#endif

using PFN_Connect    = UNSIGNED32 (ADS_CALL*)(UNSIGNED8*, UNSIGNED16, UNSIGNED8*,
                                              UNSIGNED8*, UNSIGNED32, ADSHANDLE*);
using PFN_Disconnect = UNSIGNED32 (ADS_CALL*)(ADSHANDLE);
using PFN_CreateStmt = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, ADSHANDLE*);
using PFN_ExecSQL    = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, UNSIGNED8*, ADSHANDLE*);
using PFN_Close      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE);
using PFN_AtEOF      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, UNSIGNED16*);
using PFN_Skip       = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, SIGNED32);
using PFN_GetField   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, UNSIGNED8*, UNSIGNED8*,
                                              UNSIGNED32*, UNSIGNED16);
using PFN_NumFields  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, UNSIGNED16*);
using PFN_FieldName  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE, UNSIGNED16, UNSIGNED8*,
                                              UNSIGNED16*);
using PFN_LastError  = UNSIGNED32 (ADS_CALL*)(UNSIGNED32*, UNSIGNED8*,
                                              UNSIGNED16*);

struct Api {
    PFN_Connect    connect;
    PFN_Disconnect disconnect;
    PFN_CreateStmt createStmt;
    PFN_ExecSQL    execSQL;
    PFN_Close      close;
    PFN_AtEOF      atEOF;
    PFN_Skip       skip;
    PFN_GetField   getField;
    PFN_NumFields  numFields;
    PFN_FieldName  fieldName;
    PFN_Disconnect gotoTop;   // AdsGotoTop(handle) — same signature shape
    PFN_LastError  lastError = nullptr;
    bool           do_gototop = false;
};

// ADS_LOCAL_SERVER = 1.
static constexpr UNSIGNED16 ADS_LOCAL_SERVER = 1;

// ---------------------------------------------------------------------------
// JSON helpers (minimal, string-based)
// ---------------------------------------------------------------------------
static std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

static std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// Run one SQL query and emit its rows as a JSON array on `out`.
// Returns true on success. On failure emits {"error":"rc=N"} and returns false.
// ---------------------------------------------------------------------------
static bool dump_query(const Api& f, ADSHANDLE hConn, const char* sql,
                       std::string& out) {
    ADSHANDLE hStmt = 0;
    if (f.createStmt(hConn, &hStmt) != 0) {
        out += "{\"error\":\"AdsCreateSQLStatement failed\"}";
        return false;
    }
    ADSHANDLE hCur = 0;
    std::string q = sql;
    UNSIGNED32 rc = f.execSQL(hStmt,
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(q.c_str())), &hCur);
    if (rc != 0 || hCur == 0) {
        // RCB 07/17/2026: include AdsGetLastError text — indispensable when
        // probing script-engine failures.
        char emsg[600] = {0};
        if (f.lastError) {
            UNSIGNED32 ecode = 0;
            UNSIGNED16 elen = sizeof(emsg) - 1;
            f.lastError(&ecode, reinterpret_cast<UNSIGNED8*>(emsg), &elen);
            emsg[elen < sizeof(emsg) ? elen : sizeof(emsg) - 1] = 0;
        }
        char buf[700];
        std::snprintf(buf, sizeof(buf), "{\"error\":\"rc=%u\",\"msg\":\"", rc);
        out += buf;
        for (const char* p = emsg; *p; ++p) {
            if (*p == '"' || *p == '\\') out += '\\';
            if (static_cast<unsigned char>(*p) >= 0x20) out += *p;
        }
        out += "\"}";
        f.close(hStmt);
        return false;
    }

    if (f.do_gototop && f.gotoTop) f.gotoTop(hCur);  // normalize cursor to row 1
    UNSIGNED16 nfields = 0;
    f.numFields(hCur, &nfields);
    // Cache field names once (schema is stable across the result set).
    std::vector<std::string> names(nfields);
    for (UNSIGNED16 i = 1; i <= nfields; ++i) {
        UNSIGNED8  nm[256] = {0};
        UNSIGNED16 cap = sizeof(nm) - 1;
        if (f.fieldName(hCur, i, nm, &cap) == 0) {
            names[i - 1].assign(reinterpret_cast<char*>(nm), cap);
        }
    }

    out += "[";
    UNSIGNED16 eof = 0;
    f.atEOF(hCur, &eof);
    bool first_row = true;
    std::vector<UNSIGNED8> val(65536);
    while (!eof) {
        if (!first_row) out += ",";
        first_row = false;
        out += "{";
        for (UNSIGNED16 i = 1; i <= nfields; ++i) {
            if (i > 1) out += ",";
            UNSIGNED32 vlen = static_cast<UNSIGNED32>(val.size() - 1);
            std::string v;
            UNSIGNED8 fldname[256] = {0};
            std::memcpy(fldname, names[i - 1].c_str(),
                        names[i - 1].size() < 255 ? names[i - 1].size() : 255);
            if (f.getField(hCur, fldname, val.data(), &vlen, 0) == 0) {
                v.assign(reinterpret_cast<char*>(val.data()), vlen);
            }
            out += "\"" + json_escape(names[i - 1]) + "\":\"" +
                   json_escape(rtrim(v)) + "\"";
        }
        out += "}";
        f.skip(hCur, 1);
        f.atEOF(hCur, &eof);
    }
    out += "]";
    f.close(hCur);
    f.close(hStmt);
    return true;
}

// ---------------------------------------------------------------------------
// The metadata sections. Each is a (name, SQL) pair. ORDER BY keeps the dump
// deterministic so two engine outputs diff line-for-line.
// ---------------------------------------------------------------------------
struct Section { const char* name; const char* sql; };

// No ORDER BY: the column names differ between engines (SAP vs OpenADS use
// different casing/names in their system.* tables), and an ORDER BY on a name
// one engine doesn't have fails the whole query — which would masquerade as a
// missing table. The diff step sorts rows client-side by full content instead.
static const Section kSections[] = {
    {"tables",           "SELECT * FROM system.tables"},
    {"columns",          "SELECT * FROM system.columns"},
    {"indexes",          "SELECT * FROM system.indexes"},
    {"relations",        "SELECT * FROM system.relations"},
    {"triggers",         "SELECT * FROM system.triggers"},
    {"storedprocedures", "SELECT * FROM system.storedprocedures"},
    {"functions",        "SELECT * FROM system.functions"},
    {"views",            "SELECT * FROM system.views"},
    {"users",            "SELECT * FROM system.users"},
    {"usergroups",       "SELECT * FROM system.usergroups"},
    {"usergroupmembers", "SELECT * FROM system.usergroupmembers"},
    {"permissions",      "SELECT * FROM system.permissions"},
};

int main(int argc, char** argv) {
    std::string lib, db, user, pw, only;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if      (a == "--lib")      lib  = next();
        else if (a == "--db")       db   = next();
        else if (a == "--user")     user = next();
        else if (a == "--password") pw   = next();
        else if (a == "--only")     only = next();
        else if (a == "--sql")      only = "\x01" + next();  // ad-hoc probe
    }
    if (lib.empty() || db.empty()) {
        std::fprintf(stderr,
            "usage: dd_meta_dump --lib <dll> --db <path.add> "
            "--user <name> [--password <pw>] [--only s1,s2]\n");
        return 2;
    }

    lib_handle h = lib_open(lib.c_str());
    if (!h) { std::fprintf(stderr, "cannot load %s\n", lib.c_str()); return 1; }

    Api f{};
    f.connect    = (PFN_Connect)    lib_sym(h, "AdsConnect60");
    f.disconnect = (PFN_Disconnect) lib_sym(h, "AdsDisconnect");
    f.createStmt = (PFN_CreateStmt) lib_sym(h, "AdsCreateSQLStatement");
    f.execSQL    = (PFN_ExecSQL)    lib_sym(h, "AdsExecuteSQLDirect");
    f.close      = (PFN_Close)      lib_sym(h, "AdsCloseTable");
    f.atEOF      = (PFN_AtEOF)      lib_sym(h, "AdsAtEOF");
    f.skip       = (PFN_Skip)       lib_sym(h, "AdsSkip");
    f.getField   = (PFN_GetField)   lib_sym(h, "AdsGetField");
    f.numFields  = (PFN_NumFields)  lib_sym(h, "AdsGetNumFields");
    f.fieldName  = (PFN_FieldName)  lib_sym(h, "AdsGetFieldName");
    f.gotoTop    = (PFN_Disconnect) lib_sym(h, "AdsGotoTop");
    f.lastError  = (PFN_LastError)  lib_sym(h, "AdsGetLastError");
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--gototop") f.do_gototop = true;
    if (!f.connect || !f.createStmt || !f.execSQL || !f.atEOF ||
        !f.skip || !f.getField || !f.numFields || !f.fieldName) {
        std::fprintf(stderr, "DLL missing required Ads* exports\n");
        return 1;
    }

    ADSHANDLE hConn = 0;
    std::string dbz = db, userz = user, pwz = pw;
    UNSIGNED32 rc = f.connect(
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(dbz.c_str())),
        ADS_LOCAL_SERVER,
        user.empty() ? nullptr
                     : reinterpret_cast<UNSIGNED8*>(const_cast<char*>(userz.c_str())),
        pw.empty()   ? nullptr
                     : reinterpret_cast<UNSIGNED8*>(const_cast<char*>(pwz.c_str())),
        0, &hConn);
    if (rc != 0) {
        std::fprintf(stderr, "AdsConnect60 failed rc=%u (db=%s user=%s)\n",
                     rc, db.c_str(), user.c_str());
        return 1;
    }

    // Ad-hoc single-query probe: --sql "<query>".
    if (!only.empty() && only[0] == '\x01') {
        std::string out;
        dump_query(f, hConn, only.substr(1).c_str(), out);
        std::fputs(out.c_str(), stdout);
        std::fputc('\n', stdout);
        f.disconnect(hConn);
        return 0;
    }

    std::string out = "{\n";
    bool first = true;
    for (const auto& s : kSections) {
        if (!only.empty() &&
            (std::string(",") + only + ",").find(std::string(",") + s.name + ",")
                == std::string::npos) {
            continue;
        }
        if (!first) out += ",\n";
        first = false;
        out += "  \"";
        out += s.name;
        out += "\": ";
        dump_query(f, hConn, s.sql, out);
    }
    out += "\n}\n";

    std::fputs(out.c_str(), stdout);
    f.disconnect(hConn);
    return 0;
}
