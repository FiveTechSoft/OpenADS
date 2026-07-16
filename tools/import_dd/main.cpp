// openads_import_dd — one-shot SAP ADS → OpenADS data dictionary importer.
//
// Reads DB:-group memberships and fine-grained object permissions from an
// existing SAP ADS .add file using the SAP ACE runtime, then writes them
// into a copy of that file using the OpenADS DataDict API.  After the
// import the copy is a fully self-contained OpenADS DD that no longer
// requires the SAP DLL for permission enforcement.
//
// Usage:
//   openads_import_dd --source <sap.add> --dest <copy.add>
//                     --user <name> --password <pw>
//                     [--sap-lib <path/to/ace64.dll>]
//                     [--no-copy]   (dest already exists, skip the copy step)
//
// Output: JSON to stdout.
//   { "ok": true,  "memberships": N, "permissions": N, "warnings": [...] }
//   { "ok": false, "error": "...",                      "warnings": [...] }
//
// Called by administration clients or scripts; callers parse the JSON result.

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include "engine/data_dict.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Cross-platform dynamic library loader
// ---------------------------------------------------------------------------

#ifdef _WIN32
using lib_handle = HMODULE;
static lib_handle lib_open(const char* path) {
    // Set the DLL search directory to the directory containing ace64.dll so that
    // its dependencies (adsloc64.dll, aicu64.dll, axcws64.dll) are loaded from
    // the same version set.  Without this, Windows finds them via PATH — potentially
    // a mismatched version in "Common Files\Advantage" — and AdsConnect60 fails
    // with error 7078.
    std::string p = path;
    auto sep = p.find_last_of("\\/");
    if (sep != std::string::npos) {
        std::string dir = p.substr(0, sep);
        SetDllDirectoryA(dir.c_str());
    }
    HMODULE h = LoadLibraryA(path);
    SetDllDirectoryA(nullptr);  // restore default search order
    return h;
}
static void*      lib_sym(lib_handle h, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(h, name));
}
static void       lib_close(lib_handle h)       { FreeLibrary(h); }
static std::string lib_error() {
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
                   0, buf, sizeof(buf) - 1, nullptr);
    auto n = strlen(buf);
    while (n && (buf[n-1]=='\r'||buf[n-1]=='\n')) buf[--n]='\0';
    return buf;
}
#else
using lib_handle = void*;
static lib_handle lib_open(const char* path)    { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void*      lib_sym(lib_handle h, const char* name) { return dlsym(h, name); }
static void       lib_close(lib_handle h)       { dlclose(h); }
static std::string lib_error()                  { const char* e = dlerror(); return e ? e : "unknown"; }
#endif

// ---------------------------------------------------------------------------
// SAP ACE function pointer typedefs
// ---------------------------------------------------------------------------

using UNSIGNED8  = unsigned char;
using UNSIGNED16 = unsigned short;
using UNSIGNED32 = unsigned int;
using SIGNED32   = int;
using ADSHANDLE  = unsigned long long;

// SAP ACE uses __stdcall on Windows x86/x64; on Linux/macOS all functions
// follow the platform ABI and __stdcall is not applicable.
#ifdef _WIN32
#  define ADS_CALL __stdcall
#else
#  define ADS_CALL
#endif

using PFN_Connect    = UNSIGNED32 (ADS_CALL*)(UNSIGNED8* path, UNSIGNED16 srv,
                           UNSIGNED8* user, UNSIGNED8* pw,
                           UNSIGNED32 opts, ADSHANDLE* ph);
using PFN_Disconnect = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h);
using PFN_CreateStmt = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hConn, ADSHANDLE* phStmt);
using PFN_ExecSQL    = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hStmt, UNSIGNED8* sql, ADSHANDLE* phc);
using PFN_Close      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h);
using PFN_AtEOF      = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16* pb);
using PFN_Skip       = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, SIGNED32 n);
using PFN_GetField   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED8* name,
                           UNSIGNED8* buf, UNSIGNED32* len, UNSIGNED16 raw);
using PFN_NumFields  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16* n);
using PFN_FieldName  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16 idx,
                           UNSIGNED8* buf, UNSIGNED16* len);
using PFN_GetPerms   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED8* grantee,
                           UNSIGNED16 obj_type, UNSIGNED8* obj_name,
                           UNSIGNED8* parent, UNSIGNED16 inherited,
                           UNSIGNED32* out);
using PFN_FindFirst  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, UNSIGNED16 findType,
                           UNSIGNED8* parent, UNSIGNED8* nameBuf,
                           UNSIGNED16* nameLen, ADSHANDLE* phFind);
using PFN_FindNext   = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, ADSHANDLE hFind,
                           UNSIGNED8* nameBuf, UNSIGNED16* nameLen);
using PFN_FindClose  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE hObj, ADSHANDLE hFind);
using PFN_GetDBProp  = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED16 usProp,
                           void* pBuf, UNSIGNED16* pusLen);
using PFN_GetTableProp = UNSIGNED32 (ADS_CALL*)(ADSHANDLE h, UNSIGNED8* table,
                           UNSIGNED16 usProp, void* pBuf, UNSIGNED16* pusLen);

struct SapFuncs {
    PFN_Connect    connect    = nullptr;
    PFN_Disconnect disconnect = nullptr;
    PFN_CreateStmt createStmt = nullptr;
    PFN_ExecSQL    execSQL    = nullptr;
    PFN_Close      close      = nullptr;
    PFN_AtEOF      atEOF      = nullptr;
    PFN_Skip       skip       = nullptr;
    PFN_GetField   getField   = nullptr;
    PFN_NumFields  numFields  = nullptr;
    PFN_FieldName  fieldName  = nullptr;
    PFN_GetPerms   getPerms   = nullptr;
    PFN_FindFirst  findFirst  = nullptr;
    PFN_FindNext   findNext   = nullptr;
    PFN_FindClose  findClose  = nullptr;
    PFN_GetDBProp  getDBProp  = nullptr;
    PFN_GetTableProp getTableProp = nullptr;
};

// ---------------------------------------------------------------------------
// JSON helpers (no external dependency)
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char raw : s) {
        auto c = static_cast<unsigned char>(raw);
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 0x20)  { char buf[8]; std::snprintf(buf,8,"\\u%04X",c); out+=buf; }
        else                out += static_cast<char>(c);
    }
    return out;
}

static void emit_result(bool ok,
                        int memberships, int permissions,
                        int function_bodies, int db_properties,
                        const std::string& error,
                        const std::vector<std::string>& warnings) {
    std::printf("{\"ok\":%s", ok ? "true" : "false");
    if (ok) {
        std::printf(",\"memberships\":%d,\"permissions\":%d,\"function_bodies\":%d,\"db_properties\":%d",
                    memberships, permissions, function_bodies, db_properties);
    } else {
        std::printf(",\"error\":\"%s\"", json_escape(error).c_str());
    }
    std::printf(",\"warnings\":[");
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) std::printf(",");
        std::printf("\"%s\"", json_escape(warnings[i]).c_str());
    }
    std::printf("]}\n");
}

// ---------------------------------------------------------------------------
// SAP query helpers
// ---------------------------------------------------------------------------

static std::string sap_field(const SapFuncs& f, ADSHANDLE hc, const char* name) {
    char buf[512] = {};
    UNSIGNED32 len = sizeof(buf) - 1;
    f.getField(hc, (UNSIGNED8*)name, (UNSIGNED8*)buf, &len, 0);
    // Trim trailing spaces (SAP CHAR fields are space-padded).
    auto n = strlen(buf);
    while (n && buf[n-1] == ' ') buf[--n] = '\0';
    return buf;
}

// Variant with a larger buffer for potentially long fields (e.g. function bodies).
static std::string sap_field_large(const SapFuncs& f, ADSHANDLE hc,
                                    const char* name, std::size_t maxLen = 8192) {
    std::vector<char> buf(maxLen + 1, '\0');
    UNSIGNED32 len = static_cast<UNSIGNED32>(maxLen);
    f.getField(hc, (UNSIGNED8*)name, (UNSIGNED8*)buf.data(), &len, 0);
    // AdsGetField sets len to the actual byte count filled.
    while (len && buf[len - 1] == ' ') --len;
    return std::string(buf.data(), len);
}

static std::string trim_sap_string(const char* buf, UNSIGNED16 len) {
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\0')) --len;
    return std::string(buf, len);
}

static std::string le_u16_string(const char* buf, UNSIGNED16 len) {
    unsigned v = 0;
    if (len >= 2) {
        v = static_cast<unsigned>(static_cast<unsigned char>(buf[0])) |
            (static_cast<unsigned>(static_cast<unsigned char>(buf[1])) << 8);
    } else if (len == 1) {
        v = static_cast<unsigned>(static_cast<unsigned char>(buf[0]));
    }
    return std::to_string(v);
}

static bool get_sap_table_prop_string(const SapFuncs& f, ADSHANDLE hConn,
                                      const std::string& table,
                                      UNSIGNED16 prop,
                                      std::string& out,
                                      UNSIGNED32& rc_out) {
    char buf[8192] = {};
    UNSIGNED16 len = static_cast<UNSIGNED16>(sizeof(buf) - 1);
    rc_out = f.getTableProp(hConn, (UNSIGNED8*)table.c_str(), prop, buf, &len);
    if (rc_out != 0) return false;
    out = trim_sap_string(buf, len);
    return true;
}

static bool get_sap_table_prop_u16(const SapFuncs& f, ADSHANDLE hConn,
                                   const std::string& table,
                                   UNSIGNED16 prop,
                                   std::string& out,
                                   UNSIGNED32& rc_out) {
    char buf[2] = {};
    UNSIGNED16 len = static_cast<UNSIGNED16>(sizeof(buf));
    rc_out = f.getTableProp(hConn, (UNSIGNED8*)table.c_str(), prop, buf, &len);
    if (rc_out != 0) return false;
    out = le_u16_string(buf, len);
    return true;
}


// ---------------------------------------------------------------------------
// Default SAP library search paths
// ---------------------------------------------------------------------------

static std::vector<std::string> default_sap_paths() {
#ifdef _WIN32
    return {
        "f:\\ads11\\ace64.dll",
        "C:\\Program Files (x86)\\Advantage 11.10\\ace64.dll",
        "C:\\Program Files (x86)\\Advantage 10.10\\ace64.dll",
        "ace64.dll",
    };
#elif defined(__APPLE__)
    return { "/usr/local/lib/libace64.dylib", "libace64.dylib" };
#else
    return { "/usr/lib/libace64.so", "/opt/ads/lib/libace64.so", "libace64.so" };
#endif
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string source, dest, user, password, sap_lib_path;
    bool no_copy = false;
    std::vector<std::string> warnings;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string(argv[i]);
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string{};
        };
        if      (arg == "--source")   source       = next();
        else if (arg == "--dest")     dest         = next();
        else if (arg == "--user")     user         = next();
        else if (arg == "--password") password     = next();
        else if (arg == "--sap-lib")  sap_lib_path = next();
        else if (arg == "--no-copy")  no_copy      = true;
    }

    if (source.empty() || dest.empty() || user.empty()) {
        emit_result(false, 0, 0, 0, 0,
            "Usage: openads_import_dd --source <sap.add> --dest <copy.add> "
            "--user <name> --password <pw> [--sap-lib <path>] [--no-copy]",
            warnings);
        return 1;
    }

    // ── Step 1: copy source → dest (and companion .am memo file) ───────────────
    if (!no_copy) {
        std::error_code ec;
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            emit_result(false, 0, 0, 0, 0,
                "Cannot copy " + source + " → " + dest + ": " + ec.message(),
                warnings);
            return 1;
        }
        // Also copy the companion .am memo file (holds SP body continuations).
        auto srcAm = fs::path(source).replace_extension(".am");
        auto dstAm = fs::path(dest).replace_extension(".am");
        if (fs::exists(srcAm)) {
            std::error_code ec2;
            fs::copy_file(srcAm, dstAm, fs::copy_options::overwrite_existing, ec2);
            if (ec2)
                warnings.push_back("Cannot copy .am memo file: " + ec2.message());
        }
    }

    // ── Step 2: load SAP DLL ─────────────────────────────────────────────────
    // If the user gave a directory instead of a .dll path, append the DLL name.
    if (!sap_lib_path.empty() && fs::is_directory(sap_lib_path)) {
        sap_lib_path = (fs::path(sap_lib_path) / "ace64.dll").string();
    }

    lib_handle sap = nullptr;
    if (!sap_lib_path.empty()) {
        sap = lib_open(sap_lib_path.c_str());
    } else {
        for (const auto& p : default_sap_paths()) {
            sap = lib_open(p.c_str());
            if (sap) break;
        }
    }
    if (!sap) {
        emit_result(false, 0, 0, 0, 0,
            "Cannot load SAP ACE library: " + lib_error() +
            ". Specify the path with --sap-lib.",
            warnings);
        return 1;
    }

    // ── Step 3: resolve SAP functions ────────────────────────────────────────
    // All resolved by name on every platform.  SAP ACE exports named symbols
    // in every version's import library and DLL — names are stable across
    // versions; ordinals are not.
    SapFuncs f;
    f.connect    = (PFN_Connect)    lib_sym(sap, "AdsConnect60");
    f.disconnect = (PFN_Disconnect) lib_sym(sap, "AdsDisconnect");
    f.execSQL    = (PFN_ExecSQL)    lib_sym(sap, "AdsExecuteSQLDirect");
    f.close      = (PFN_Close)      lib_sym(sap, "AdsCloseTable");
    f.createStmt = (PFN_CreateStmt) lib_sym(sap, "AdsCreateSQLStatement");
    f.atEOF      = (PFN_AtEOF)     lib_sym(sap, "AdsAtEOF");
    f.skip       = (PFN_Skip)      lib_sym(sap, "AdsSkip");
    f.getField   = (PFN_GetField)  lib_sym(sap, "AdsGetField");
    f.numFields  = (PFN_NumFields) lib_sym(sap, "AdsGetNumFields");
    f.fieldName  = (PFN_FieldName) lib_sym(sap, "AdsGetFieldName");
    f.getPerms   = (PFN_GetPerms)  lib_sym(sap, "AdsDDGetPermissions");
    f.findFirst  = (PFN_FindFirst) lib_sym(sap, "AdsDDFindFirstObject");
    f.findNext   = (PFN_FindNext)  lib_sym(sap, "AdsDDFindNextObject");
    f.findClose  = (PFN_FindClose) lib_sym(sap, "AdsDDFindClose");
    f.getDBProp  = (PFN_GetDBProp) lib_sym(sap, "AdsDDGetDatabaseProperty");
    f.getTableProp = (PFN_GetTableProp) lib_sym(sap, "AdsDDGetTableProperty");

    if (!f.connect || !f.disconnect || !f.execSQL || !f.close ||
        !f.createStmt || !f.atEOF || !f.skip || !f.getField) {
        lib_close(sap);
        emit_result(false, 0, 0, 0, 0, "SAP library missing required exports.", warnings);
        return 1;
    }

    // ── Step 4: connect to SOURCE via SAP LOCAL server ────────────────────
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = f.connect(
        (UNSIGNED8*)source.c_str(), 1,
        (UNSIGNED8*)user.c_str(),
        (UNSIGNED8*)password.c_str(),
        0, &hConn);
    if (rc != 0) {
        lib_close(sap);
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "SAP connect failed (rc=%u). Check credentials and source path.", rc);
        emit_result(false, 0, 0, 0, 0, msg, warnings);
        return 1;
    }

    ADSHANDLE hStmt = 0;
    rc = f.createStmt(hConn, &hStmt);
    if (rc != 0 || !hStmt) {
        f.disconnect(hConn);
        lib_close(sap);
        emit_result(false, 0, 0, 0, 0, "AdsCreateSQLStatement failed.", warnings);
        return 1;
    }

    // ── Step 5: read all user→group memberships ──────────────────────────────
    // We read every membership from SAP, then re-write them using add_user_to_group
    // (which sets prop_null=false).  clear_sap_permissions() later wipes the original
    // SAP-written records (prop_null=true), so pre-importing ALL memberships is
    // essential — otherwise regular groups (Administrators, Supervisors, etc.) vanish.
    struct Membership { std::string user, group; };
    std::vector<Membership> memberships;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT User_Name, Group_Name "
                        "FROM system.usergroupmembers "
                        "ORDER BY User_Name, Group_Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                auto u = sap_field(f, hc, "User_Name");
                auto g = sap_field(f, hc, "Group_Name");
                if (!u.empty() && !g.empty())
                    memberships.push_back({u, g});
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.usergroupmembers query failed — group memberships skipped.");
        }
    }

    // ── Step 5b: read user-defined function bodies from system.functions ────────
    // SAP stores function bodies in an encrypted binary blob inside the .add
    // record.  system.functions decrypts and returns the readable SQL body.
    // We overwrite the encrypted blobs in the dest .add with a readable lstr
    // layout so proc_body.php can display them without needing the SAP DLL.
    //
    // system.functions column names (SAP 11.x):
    //   Name | Package | Return Type | Input Parameters | Implementation | Comment
    struct FuncBody {
        std::string name, return_type, input_params, body;
    };
    std::vector<FuncBody> func_bodies;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, [Return Type], [Input Parameters], Implementation "
                        "FROM system.functions ORDER BY Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                FuncBody fb;
                fb.name         = sap_field(f, hc, "Name");
                fb.return_type  = sap_field(f, hc, "Return Type");
                fb.input_params = sap_field(f, hc, "Input Parameters");
                fb.body         = sap_field_large(f, hc, "Implementation");
                if (!fb.name.empty())
                    func_bodies.push_back(std::move(fb));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.functions query failed — function bodies skipped.");
        }
    }

    // ── Step 5c: read stored-procedure input/output params from SAP ──────────
    // SAP stores params in the property zone of each StoredProc record.  OpenADS
    // loses them when it rewrites the record (join_nul truncates at 273 bytes).
    // We query system.storedprocedures while the SAP connection is still open,
    // then patch the dest .am continuation after step 8 so proc_body.php can
    // read the params even though they no longer fit in the inline property area.
    //
    // SAP system.storedprocedures columns (version 11.x):
    //   Name | Proc_Input | Proc_Output | SQL_Script | …
    struct ProcParams {
        std::string name, input_params, output_params;
    };
    std::vector<ProcParams> proc_params;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, Proc_Input, Proc_Output "
                        "FROM system.storedprocedures ORDER BY Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                ProcParams pp;
                pp.name          = sap_field(f, hc, "Name");
                pp.input_params  = sap_field(f, hc, "Proc_Input");
                pp.output_params = sap_field(f, hc, "Proc_Output");
                if (!pp.name.empty() && (!pp.input_params.empty() || !pp.output_params.empty()))
                    proc_params.push_back(std::move(pp));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.storedprocedures query failed — SP params skipped.");
        }
    }

    // ── Step 5c2: read per-tag index metadata from SAP ──────────────────────
    // RCB 07/16/2026: SAP's system.indexes is one row per index TAG with the
    // real key expression, condition, options, key length and collation — data
    // OpenADS's DD never captured (it only knew index FILES). We read it here
    // (SAP ACE decodes it fully) and write per-tag Index records into the dest
    // DD so `SELECT * FROM system.indexes` matches SAP.
    struct IdxMeta {
        std::string name, parent, file, expr, cond, opts, keylen, coll, comment;
    };
    std::vector<IdxMeta> idx_meta;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, Parent, Index_File_Name, Index_Expression, "
                        "Index_Condition, Index_Options, Index_Key_Length, "
                        "Index_Collation, Comment FROM system.indexes",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                IdxMeta im;
                im.name    = sap_field(f, hc, "Name");
                im.parent  = sap_field(f, hc, "Parent");
                im.file    = sap_field(f, hc, "Index_File_Name");
                im.expr    = sap_field(f, hc, "Index_Expression");
                im.cond    = sap_field(f, hc, "Index_Condition");
                im.opts    = sap_field(f, hc, "Index_Options");
                im.keylen  = sap_field(f, hc, "Index_Key_Length");
                im.coll    = sap_field(f, hc, "Index_Collation");
                im.comment = sap_field(f, hc, "Comment");
                if (!im.name.empty() && !im.parent.empty())
                    idx_meta.push_back(std::move(im));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.indexes query failed — index metadata skipped.");
        }
    }

    // ── Step 5d: read table properties from SAP ─────────────────────────────
    struct TableProps {
        std::string name;
        std::unordered_map<int, std::string> props;
    };
    std::vector<TableProps> table_props_list;
    {
        std::vector<std::string> table_names;
        if (f.findFirst && f.findNext && f.findClose) {
            char nameBuf[256] = {};
            UNSIGNED16 nameLen = sizeof(nameBuf) - 1;
            ADSHANDLE hFind = 0;
            UNSIGNED32 rc2 = f.findFirst(hConn, 1, nullptr,
                                         (UNSIGNED8*)nameBuf, &nameLen, &hFind);
            while (rc2 == 0 && hFind) {
                nameBuf[nameLen] = '\0';
                while (nameLen > 0 && nameBuf[nameLen - 1] == ' ')
                    nameBuf[--nameLen] = '\0';
                if (nameBuf[0]) table_names.emplace_back(nameBuf);
                nameLen = sizeof(nameBuf) - 1;
                nameBuf[0] = '\0';
                rc2 = f.findNext(hConn, hFind, (UNSIGNED8*)nameBuf, &nameLen);
            }
            if (hFind) f.findClose(hConn, hFind);
        }

        if (table_names.empty()) {
            ADSHANDLE hc = 0;
            rc = f.execSQL(hStmt,
                (UNSIGNED8*)"SELECT Name FROM system.tables ORDER BY Name",
                &hc);
            if (rc == 0 && hc) {
                UNSIGNED16 eof = 0;
                while (f.atEOF(hc, &eof) == 0 && !eof) {
                    auto name = sap_field(f, hc, "Name");
                    if (!name.empty()) table_names.push_back(std::move(name));
                    f.skip(hc, 1);
                }
                f.close(hc);
            }
        }

        if (!f.getTableProp) {
            warnings.push_back("AdsDDGetTableProperty not found in SAP library — table properties skipped.");
        } else if (table_names.empty()) {
            warnings.push_back("No SAP table names found — table properties skipped.");
        } else {
            struct PropDef { UNSIGNED16 sap_id; int openads_id; bool is_u16; const char* name; };
            static const PropDef kTableProps[] = {
                { 1,   1,   false, "comment" },
                { 3,   3,   false, "user_defined" },
                { 22,  3,   false, "user_defined" },
                { 200, 200, false, "validation_expr" },
                { 201, 201, false, "validation_msg" },
                { 202, 202, false, "primary_key" },
                { 203, 203, true,  "auto_create" },
                { 213, 213, false, "default_index" },
                { 214, 214, true,  "encryption" },
                { 215, 215, true,  "memo_block_size" },
                { 216, 216, true,  "permission_level" },
                { 217, 217, true,  "caching" },
                { 218, 218, true,  "txn_free" },
            };

            for (const auto& table : table_names) {
                TableProps tp;
                tp.name = table;
                for (const auto& pd : kTableProps) {
                    std::string value;
                    UNSIGNED32 prc = 0;
                    bool ok = pd.is_u16
                        ? get_sap_table_prop_u16(f, hConn, table, pd.sap_id, value, prc)
                        : get_sap_table_prop_string(f, hConn, table, pd.sap_id, value, prc);
                    if (!ok) continue;
                    if (!pd.is_u16 && value.empty()) continue;
                    if (pd.openads_id == 3 && tp.props.count(3) != 0 && value.empty()) continue;
                    tp.props[pd.openads_id] = value;
                }
                if (!tp.props.empty()) table_props_list.push_back(std::move(tp));
            }
        }
    }

    // ── Step 5e: read trigger timing from SAP system.triggers ────────────────
    // SAP stores Trig_Trigger_Type (1=BEFORE 2=INSTEAD_OF 4=AFTER) in the DD
    // binary property area.  OpenADS reconstructs triggers from the binary but
    // may not capture timing correctly; we read it via SQL and patch the .am
    // JSON after the DataDict scope closes (Step 8c).
    struct TriggerTiming { std::string name, table_name; std::uint32_t timing = 0; };
    std::vector<TriggerTiming> trigger_timings;
    {
        ADSHANDLE hc = 0;
        rc = f.execSQL(hStmt,
            (UNSIGNED8*)"SELECT Name, Trig_TableName, Trig_Trigger_Type "
                        "FROM system.triggers ORDER BY Trig_TableName, Name",
            &hc);
        if (rc == 0 && hc) {
            UNSIGNED16 eof = 0;
            while (f.atEOF(hc, &eof) == 0 && !eof) {
                TriggerTiming tt;
                tt.name       = sap_field(f, hc, "Name");
                tt.table_name = sap_field(f, hc, "Trig_TableName");
                tt.timing     = static_cast<std::uint32_t>(
                                    std::atoi(sap_field(f, hc, "Trig_Trigger_Type").c_str()));
                if (!tt.name.empty() && !tt.table_name.empty())
                    trigger_timings.push_back(std::move(tt));
                f.skip(hc, 1);
            }
            f.close(hc);
        } else {
            warnings.push_back("system.triggers query failed — trigger timings skipped.");
        }
    }

    // ── Step 5f: read database-level properties from SAP ─────────────────────
    // SAP database property IDs (from ace.h, 100+ range) differ from OpenADS
    // internal storage keys.  The table below maps each SAP ID to the OpenADS
    // "prop_N" key that SP_MODIFYDATABASE / the engine uses, so that db_props.php
    // and the login-required check in AdsConnect60 can find the values.
    // Skip: 101=ADMIN_PASSWORD, 105=ENCRYPT_TABLE_PASSWORD (write-only/sensitive).
    struct DBPropDef { UNSIGNED16 sap_id; bool is_u16; const char* openads_key; };
    static const DBPropDef kDBProps[] = {
        //  sap_id   u16    openads key   — SAP name
        {    1, false, "prop_1"  }, // COMMENT (generic object prop, works for DB)
        {  100, false, "prop_3"  }, // DEFAULT_TABLE_PATH
        {  102, false, "prop_12" }, // TEMP_TABLE_PATH
        {  103, true,  "prop_5"  }, // LOG_IN_REQUIRED
        {  104, true,  "prop_8"  }, // VERIFY_ACCESS_RIGHTS
        {  106, true,  "prop_10" }, // ENCRYPT_NEW_TABLE
        {  111, true,  "prop_14" }, // VERSION_MAJOR
        {  112, true,  "prop_15" }, // VERSION_MINOR
        {  113, true,  "prop_16" }, // LOGINS_DISABLED
        {  114, false, "prop_17" }, // LOGINS_DISABLED_ERRSTR
        {  115, false, "prop_18" }, // FTS_DELIMITERS
        {  116, false, "prop_19" }, // FTS_NOISE
        {  117, false, "prop_20" }, // FTS_DROP_CHARS
        {  118, false, "prop_21" }, // FTS_CONDITIONAL_CHARS
        {  119, true,  "prop_22" }, // ENCRYPTED (read-only state flag)
        {  120, true,  "prop_23" }, // ENCRYPT_INDEXES
        {  122, true,  "prop_25" }, // ENCRYPT_COMMUNICATION
        {    3, false, "prop_26" }, // USER_DEFINED_PROP (generic object prop ID 3)
    };

    struct DBPropValue { std::string openads_key; std::string value; };
    std::vector<DBPropValue> db_prop_values;

    if (f.getDBProp) {
        for (const auto& pd : kDBProps) {
            char buf[4096] = {};
            // UNSIGNED16 properties require exactly 2 bytes input length (SAP returns
            // AE_INVALID_PROPERTY_LENGTH=5133 if a larger buffer length is passed).
            UNSIGNED16 len = pd.is_u16
                ? static_cast<UNSIGNED16>(sizeof(UNSIGNED16))
                : static_cast<UNSIGNED16>(sizeof(buf) - 1);
            UNSIGNED32 prc = f.getDBProp(hConn, pd.sap_id, buf, &len);
            if (prc != 0) {
                // Warn for every failure (including "not set") so users can see
                // exactly which SAP properties are available on their server.
                char wmsg[80];
                std::snprintf(wmsg, sizeof(wmsg),
                    "DB prop sap_id=%u rc=%u", (unsigned)pd.sap_id, (unsigned)prc);
                warnings.push_back(wmsg);
                continue;
            }
            if (pd.is_u16) {
                UNSIGNED16 val = 0;
                if (len >= 2)
                    val = static_cast<UNSIGNED16>(
                        static_cast<unsigned char>(buf[0]) |
                        (static_cast<unsigned char>(buf[1]) << 8));
                else if (len == 1)
                    val = static_cast<unsigned char>(buf[0]);
                // Store even 0 — "disabled/false" is a meaningful SAP state.
                db_prop_values.push_back({pd.openads_key, std::to_string(val)});
            } else {
                // Trim trailing spaces and null bytes.
                while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\0')) --len;
                if (len == 0) continue;
                db_prop_values.push_back({pd.openads_key, std::string(buf, len)});
            }
        }
        if (db_prop_values.empty()) {
            warnings.push_back("No DB properties retrieved from SAP (all returned errors).");
        }
    } else {
        warnings.push_back("AdsDDGetDatabaseProperty not found in SAP library — DB properties skipped.");
    }

    // ── Step 6: read object permissions via AdsDDGetPermissions ─────────────
    // system.permissions SQL returns 0 rows for SAP binary .add files because
    // the real ACLs are stored in encrypted property blobs.  Use the
    // AdsDDGetPermissions API instead, which decrypts them on the fly.
    // We enumerate all grantees × all objects using AdsDDFindFirstObject/Next
    // (avoids SQL queries to system.* which fail in local-server binary-DD mode).

    // Object type codes from ADS_DD_*_OBJECT constants in ace.h
    static const UNSIGNED16 kObjTable = 1;
    static const UNSIGNED16 kObjView  = 6;
    static const UNSIGNED16 kObjProc  = 10;
    static const UNSIGNED16 kObjFunc  = 18;
    static const UNSIGNED16 kObjUser  = 8;
    static const UNSIGNED16 kObjGroup = 9;

    struct Perm {
        std::string obj_type, obj_name, grantee;
        uint32_t    bitmask;
    };
    std::vector<Perm> perms;

    // Helper: enumerate DD objects of a given type using AdsDDFindFirst/Next.
    auto dd_enum = [&](UNSIGNED16 type) -> std::vector<std::string> {
        std::vector<std::string> result;
        if (!f.findFirst || !f.findNext || !f.findClose) return result;
        char nameBuf[256] = {};
        UNSIGNED16 nameLen = sizeof(nameBuf) - 1;
        ADSHANDLE hFind = 0;
        UNSIGNED32 rc2 = f.findFirst(hConn, type, nullptr,
                                     (UNSIGNED8*)nameBuf, &nameLen, &hFind);
        while (rc2 == 0 && hFind) {
            nameBuf[nameLen] = '\0';
            // Trim trailing spaces
            for (int i = (int)nameLen - 1; i >= 0 && nameBuf[i] == ' '; --i)
                nameBuf[i] = '\0';
            if (nameBuf[0]) result.push_back(nameBuf);
            nameLen = sizeof(nameBuf) - 1;
            nameBuf[0] = '\0';
            rc2 = f.findNext(hConn, hFind, (UNSIGNED8*)nameBuf, &nameLen);
        }
        if (hFind) f.findClose(hConn, hFind);
        return result;
    };

    if (!f.getPerms) {
        warnings.push_back("AdsDDGetPermissions not found in SAP library — ACLs skipped.");
    } else if (!f.findFirst) {
        warnings.push_back("AdsDDFindFirstObject not found in SAP library — ACLs skipped.");
    } else {
        // Collect all grantees: users and groups
        struct Grantee { std::string name; };
        std::vector<Grantee> grantees;
        for (auto& nm : dd_enum(kObjUser))  grantees.push_back({nm});
        for (auto& nm : dd_enum(kObjGroup)) grantees.push_back({nm});

        // Collect all securable objects: tables, views, stored procs, functions
        struct Obj { std::string name, type_name; UNSIGNED16 type_code; };
        std::vector<Obj> objects;
        for (auto& nm : dd_enum(kObjTable)) objects.push_back({nm, "Table",      kObjTable});
        for (auto& nm : dd_enum(kObjView))  objects.push_back({nm, "View",       kObjView});
        for (auto& nm : dd_enum(kObjProc))  objects.push_back({nm, "StoredProc", kObjProc});
        for (auto& nm : dd_enum(kObjFunc))  objects.push_back({nm, "Function",   kObjFunc});

        // Probe each (grantee × object) pair for direct permissions
        for (const auto& g : grantees) {
            for (const auto& o : objects) {
                UNSIGNED32 mask = 0;
                UNSIGNED32 prc = f.getPerms(hConn,
                    (UNSIGNED8*)g.name.c_str(),
                    o.type_code,
                    (UNSIGNED8*)o.name.c_str(),
                    nullptr,
                    0,      // direct permissions only (not inherited)
                    &mask);
                if (prc == 0 && mask != 0)
                    perms.push_back({o.type_name, o.name, g.name, mask});
            }
        }
    }

    f.close(hStmt);
    f.disconnect(hConn);
    lib_close(sap);

    // ── Step 7: open dest with OpenADS and write imported data ───────────────
    // Wrapped in a scope so DataDict (RAII) is destroyed — and the file fully
    // flushed — before we binary-patch function bodies in Step 8.
    int written_memberships = 0;
    int written_perms       = 0;
    int written_db_props    = 0;
    std::vector<std::string> admin_principals;
    {
        auto opened = openads::engine::DataDict::open(dest);
        if (!opened) {
            emit_result(false, 0, 0, 0, 0,
                "Cannot open dest DD: " + opened.error().message, warnings);
            return 1;
        }
        auto dd = std::move(opened).value();

        for (const auto& tp : table_props_list) {
            for (const auto& kv : tp.props) {
                dd.set_table_property(tp.name, kv.first, kv.second);
            }
        }

        for (const auto& pv : db_prop_values) {
            auto r = dd.set_db_property(pv.openads_key, pv.value);
            if (r) ++written_db_props;
            else warnings.push_back("set_db_property(" + pv.openads_key + "): " + r.error().message);
        }

        for (const auto& m : memberships) {
            auto r = dd.add_user_to_group(m.user, m.group);
            if (r) ++written_memberships;
            else warnings.push_back("add_user_to_group(" + m.user + "," + m.group + "): " + r.error().message);
        }

        // Per-tag index metadata: replace the file-level entries the dest DD
        // parsed from SAP with the rich per-tag set read from system.indexes.
        std::size_t written_indexes = 0;
        if (!idx_meta.empty()) {
            (void)dd.clear_indexes(/*persist=*/false);
            for (const auto& im : idx_meta) {
                openads::engine::DataDict::IndexEntry e;
                e.table_alias = im.parent;
                e.index_path  = im.file;
                e.tag_name    = im.name;
                e.expression  = im.expr;
                e.condition   = im.cond;
                e.options     = im.opts;
                e.key_length  = im.keylen;
                e.collation   = im.coll;
                e.comment     = im.comment;
                auto r = dd.add_index(e, /*persist=*/false);
                if (r) ++written_indexes;
                else warnings.push_back("add_index(" + im.parent + "." +
                                        im.name + "): " + r.error().message);
            }
            if (auto r = dd.save(); !r)
                warnings.push_back("save after indexes: " + r.error().message);
        }

        // ── Guarantee adssys user exists and is DB:Admin ──────────────────────
        // adssys is the SAP root account (equivalent to MySQL root).  SAP treats
        // it as built-in and does not expose it through system.usergroupmembers,
        // so it may not appear in the memberships list above.  Ensure the target
        // DD has adssys as a recognised user with at least DB:Admin membership so
        // OpenADS authentication still works for the admin account.
        {
            // Case-insensitive search: SAP may export the name as "ADSSYS".
            auto ci_lower = [](std::string s) {
                for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
                return s;
            };
            std::string canonical;
            for (const auto& u : dd.users()) {
                if (ci_lower(u) == "adssys") { canonical = u; break; }
            }
            if (canonical.empty()) {
                for (const auto& m : memberships) {
                    if (ci_lower(m.user) == "adssys") { canonical = m.user; break; }
                }
            }
            if (canonical.empty()) canonical = "adssys"; // default if SAP never exported it

            if (!dd.has_user(canonical)) {
                auto r = dd.create_user(canonical);
                if (!r) warnings.push_back("create_user(" + canonical + "): " + r.error().message);
                else    warnings.push_back(canonical + " created (SAP built-in, not in export)");
            }
            // Always ensure DB:Admin membership — idempotent if already present.
            {
                auto r = dd.add_user_to_group(canonical, "DB:Admin");
                if (!r) warnings.push_back("add_user_to_group(" + canonical + ",DB:Admin): " + r.error().message);
                else    ++written_memberships;
            }
            admin_principals.push_back(canonical);
            if (!user.empty() && ci_lower(user) != ci_lower(canonical)) {
                auto r = dd.add_user_to_group(user, "DB:Admin");
                if (!r) warnings.push_back("add_user_to_group(" + user + ",DB:Admin): " + r.error().message);
                else    ++written_memberships;
                admin_principals.push_back(user);
            }
        }

        for (const auto& p : perms) {
            auto r = dd.grant_permission(p.obj_type, p.obj_name, p.grantee, p.bitmask);
            if (r) ++written_perms;
            else warnings.push_back("grant_permission(" + p.obj_type + "," + p.obj_name + "," + p.grantee + "): " + r.error().message);
        }

        // RCB 06/29/2026: DB:Admin is an administrative bypass in the OpenADS
        // core, but clients commonly inspect system.permissions directly.
        // Persist explicit full grants after SAP ACL import so newly imported
        // DDs expose the same administrative reality that the engine enforces.
        constexpr std::uint32_t admin_mask =
            openads::engine::DataDict::DD_PERM_SELECT |
            openads::engine::DataDict::DD_PERM_UPDATE |
            openads::engine::DataDict::DD_PERM_EXECUTE |
            openads::engine::DataDict::DD_PERM_INSERT |
            openads::engine::DataDict::DD_PERM_DELETE |
            0x040u | 0x080u | 0x100u | 0x200u; // access/create/alter/drop
        auto grant_admin_full = [&](const std::string& grantee) {
            auto grant_one = [&](const std::string& obj_type,
                                 const std::string& obj_name) {
                auto r = dd.grant_permission(obj_type, obj_name, grantee, admin_mask);
                if (r) ++written_perms;
                else warnings.push_back("grant_permission(" + obj_type + "," +
                    obj_name + "," + grantee + "): " + r.error().message);
            };
            grant_one("Database", "Database");
            for (const auto& kv : dd.tables())    grant_one("Table", kv.first);
            for (const auto& kv : dd.views())     grant_one("View", kv.first);
            for (const auto& kv : dd.procs())     grant_one("StoredProc", kv.first);
            for (const auto& kv : dd.functions()) grant_one("Function", kv.first);
            for (const auto& kv : dd.links())     grant_one("Link", kv.first);
        };
        grant_admin_full("DB:Admin");
        for (const auto& principal : admin_principals)
            grant_admin_full(principal);

        // Remove all SAP-written encrypted Permission records from the dest file.
        // Even when system.permissions returned 0 rows (SAP stores real ACLs in
        // encrypted blobs not exposed via SQL), the original SAP records must be
        // deleted so OpenADS no longer raises AE_SAP_PERMS_NEED_IMPORT (5174).
        auto rc2 = dd.clear_sap_permissions();
        if (!rc2) warnings.push_back("clear_sap_permissions: " + rc2.error().message);
    } // DataDict destroyed and file flushed here

    // ── Step 8: binary-patch encrypted function bodies in dest .add ──────────
    // SAP stores function bodies as encrypted blobs in the .add property area.
    // OpenADS cannot decrypt them.  We overwrite them with a plain lstr layout:
    //   plen = 0 (so proc_body.php starts reading at offset 0 in property)
    //   property[0..1]   = le16(len(return_type))
    //   property[2..]    = return_type bytes
    //   next le16 + data = input_params
    //   next le16 + data = body
    // This matches the lstr branch in proc_body.php's Function handler.

    int written_func_bodies = 0;
    if (!func_bodies.empty()) {
        auto write_u16_fn = [](std::uint8_t* p, std::uint16_t v) {
            p[0] = static_cast<std::uint8_t>(v & 0xFF);
            p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };
        auto read_u32_fn = [](const std::uint8_t* p) -> std::uint32_t {
            return static_cast<std::uint32_t>(p[0])
                 | (static_cast<std::uint32_t>(p[1]) <<  8)
                 | (static_cast<std::uint32_t>(p[2]) << 16)
                 | (static_cast<std::uint32_t>(p[3]) << 24);
        };

        // Read dest .add into memory
        std::vector<std::uint8_t> addData;
        bool read_ok = false;
        {
            std::FILE* fh = std::fopen(dest.c_str(), "rb");
            if (!fh) {
                warnings.push_back("Cannot read dest .add for function body patch.");
            } else {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    addData.resize(static_cast<std::size_t>(sz));
                    [[maybe_unused]] auto _r1 = std::fread(addData.data(), 1, addData.size(), fh);
                    read_ok = true;
                }
                std::fclose(fh);
            }
        }

        if (read_ok && addData.size() >= 40) {
            std::uint32_t hdrLen = read_u32_fn(addData.data() + 0x20);
            std::uint32_t recLen = read_u32_fn(addData.data() + 0x24);

            if (recLen > 0 && hdrLen < addData.size()) {
                std::uint32_t total = static_cast<std::uint32_t>(
                    (addData.size() - hdrLen) / recLen);

                for (std::uint32_t i = 0; i < total; ++i) {
                    std::uint8_t* base = addData.data() + hdrLen + i * recLen;
                    if (base[0] != 0x04) continue;

                    // objType: bytes [13..22], 10 chars, space/NUL trimmed
                    char objType[11] = {};
                    std::memcpy(objType, base + 13, 10);
                    for (int k = 9; k >= 0 && (objType[k] == ' ' || objType[k] == '\0'); --k)
                        objType[k] = '\0';
                    if (std::strcmp(objType, "Function") != 0) continue;

                    // objName: bytes [23..222], 200 chars, space/NUL trimmed
                    char objName[201] = {};
                    std::memcpy(objName, base + 23, 200);
                    for (int k = 199; k >= 0 && (objName[k] == ' ' || objName[k] == '\0'); --k)
                        objName[k] = '\0';

                    // Find matching FuncBody (case-insensitive)
                    for (const auto& fb : func_bodies) {
#ifdef _WIN32
                        if (_stricmp(fb.name.c_str(), objName) != 0) continue;
#else
                        if (strcasecmp(fb.name.c_str(), objName) != 0) continue;
#endif
                        // Record layout: plen at [223..224], property at [225..497] (273 bytes).
                        constexpr std::uint32_t PL = 273;
                        write_u16_fn(base + 223, 0);    // plen = 0
                        std::uint8_t* PS = base + 225;
                        std::memset(PS, 0, PL);

                        std::uint32_t pos = 0;
                        auto write_lstr = [&](const std::string& s) {
                            if (PL - pos < 2) return;
                            auto slen = static_cast<std::uint16_t>(
                                (std::min)(s.size(), static_cast<std::size_t>(PL - pos - 2)));
                            write_u16_fn(PS + pos, slen);
                            pos += 2;
                            if (slen) std::memcpy(PS + pos, s.data(), slen);
                            pos += slen;
                        };

                        write_lstr(fb.return_type);
                        write_lstr(fb.input_params);
                        write_lstr(fb.body);
                        ++written_func_bodies;
                        break;
                    }
                }
            }

            // Write patched .add back to disk
            std::FILE* fh = std::fopen(dest.c_str(), "wb");
            if (!fh) {
                warnings.push_back("Cannot write patched .add for function bodies.");
            } else {
                std::fwrite(addData.data(), 1, addData.size(), fh);
                std::fclose(fh);
            }
        }
    }

    // ── Step 8b: patch .am continuation with SP input/output params ──────────
    // After OpenADS rewrites the dest .add, each StoredProc record has plen=273
    // and its .am block contains:
    //   [SQL body continuation] [NUL] [old SAP padding/binary garbage]
    //
    // We overwrite the bytes after the first NUL with:
    //   [input_params] [NUL] [output_params] [NUL]
    //
    // proc_body.php then reads these after finding the first NUL in .am.
    if (!proc_params.empty()) {
        // Re-read .add (it was modified by DataDict above)
        std::vector<std::uint8_t> addData2;
        bool read_ok2 = false;
        {
            std::FILE* fh = std::fopen(dest.c_str(), "rb");
            if (fh) {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    addData2.resize(static_cast<std::size_t>(sz));
                    [[maybe_unused]] auto _r2 = std::fread(addData2.data(), 1, addData2.size(), fh);
                    read_ok2 = true;
                }
                std::fclose(fh);
            }
        }

        // Build .am path: replace the last extension in dest with .am
        std::string amDest;
        {
            auto sl  = dest.rfind('/');
            auto bs  = dest.rfind('\\');
            std::size_t lastSlash;
            if      (sl == std::string::npos && bs == std::string::npos) lastSlash = std::string::npos;
            else if (sl == std::string::npos) lastSlash = bs;
            else if (bs == std::string::npos) lastSlash = sl;
            else lastSlash = (sl > bs) ? sl : bs;
            auto dot = dest.rfind('.');
            if (dot == std::string::npos || (lastSlash != std::string::npos && dot < lastSlash))
                amDest = dest + ".am";
            else
                amDest = dest.substr(0, dot) + ".am";
        }
        std::vector<std::uint8_t> amData;
        bool am_read_ok = false;
        {
            std::FILE* fh = std::fopen(amDest.c_str(), "rb");
            if (fh) {
                std::fseek(fh, 0, SEEK_END);
                long sz = std::ftell(fh);
                std::rewind(fh);
                if (sz > 0) {
                    amData.resize(static_cast<std::size_t>(sz));
                    [[maybe_unused]] auto _r3 = std::fread(amData.data(), 1, amData.size(), fh);
                    am_read_ok = true;
                }
                std::fclose(fh);
            }
        }

        if (read_ok2 && am_read_ok && addData2.size() >= 40) {
            auto read_u32_fn = [](const std::uint8_t* p) -> std::uint32_t {
                return static_cast<std::uint32_t>(p[0])
                     | (static_cast<std::uint32_t>(p[1]) <<  8)
                     | (static_cast<std::uint32_t>(p[2]) << 16)
                     | (static_cast<std::uint32_t>(p[3]) << 24);
            };
            auto write_u32_fn = [](std::uint8_t* p, std::uint32_t v) {
                p[0] = static_cast<std::uint8_t>(v & 0xFF);
                p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
                p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
                p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
            };

            std::uint32_t hdrLen = read_u32_fn(addData2.data() + 0x20);
            std::uint32_t recLen = read_u32_fn(addData2.data() + 0x24);
            bool am_modified = false;

            if (recLen > 0 && hdrLen < addData2.size()) {
                std::uint32_t total2 = static_cast<std::uint32_t>(
                    (addData2.size() - hdrLen) / recLen);

                for (std::uint32_t i = 0; i < total2; ++i) {
                    std::uint8_t* base = addData2.data() + hdrLen + i * recLen;
                    if (base[0] != 0x04) continue;

                    char objType[11] = {};
                    std::memcpy(objType, base + 13, 10);
                    for (int k = 9; k >= 0 && (objType[k] == ' ' || objType[k] == '\0'); --k)
                        objType[k] = '\0';
                    if (std::strcmp(objType, "StoredProc") != 0 &&
                        std::strcmp(objType, "Procedure")  != 0) continue;

                    char objName[201] = {};
                    std::memcpy(objName, base + 23, 200);
                    for (int k = 199; k >= 0 && (objName[k] == ' ' || objName[k] == '\0'); --k)
                        objName[k] = '\0';

                    // Find matching ProcParams entry
                    const ProcParams* pp = nullptr;
                    for (const auto& p2 : proc_params) {
#ifdef _WIN32
                        if (_stricmp(p2.name.c_str(), objName) == 0) { pp = &p2; break; }
#else
                        if (strcasecmp(p2.name.c_str(), objName) == 0) { pp = &p2; break; }
#endif
                    }
                    if (!pp) continue;

                    // Read am_block and am_len from more_property [498..505]
                    std::uint32_t amBlock = read_u32_fn(base + 498);
                    std::uint32_t amLen   = read_u32_fn(base + 502);
                    if (amBlock == 0 || amLen == 0) continue;

                    std::size_t amOff = static_cast<std::size_t>(amBlock) * 8;
                    if (amOff >= amData.size()) continue;
                    std::size_t amEnd = (std::min)(amOff + amLen, amData.size());

                    // Find first NUL in the .am block (end of SQL body)
                    std::size_t nulPos = amEnd; // not found sentinel
                    for (std::size_t j = amOff; j < amEnd; ++j) {
                        if (amData[j] == 0) { nulPos = j; break; }
                    }
                    if (nulPos == amEnd) continue; // no NUL → skip

                    // Write input_params\0output_params\0 after the NUL
                    std::string tail;
                    tail += pp->input_params;
                    tail += '\0';
                    tail += pp->output_params;
                    tail += '\0';

                    std::size_t writeOff = nulPos + 1; // byte after the SQL-body NUL
                    std::size_t avail    = amEnd - writeOff;
                    if (tail.size() > avail) {
                        // Params don't fit; extend .am buffer and update am_len
                        std::size_t newEnd = writeOff + tail.size();
                        if (newEnd > amData.size()) amData.resize(newEnd, 0);
                        std::uint32_t newLen = static_cast<std::uint32_t>(
                            (newEnd - amOff));
                        write_u32_fn(base + 502, newLen);
                    }
                    std::memcpy(amData.data() + writeOff, tail.data(), tail.size());
                    am_modified = true;
                }
            }

            if (am_modified) {
                // Write patched .add back (am_len fields may have changed)
                {
                    std::FILE* fh = std::fopen(dest.c_str(), "wb");
                    if (fh) {
                        std::fwrite(addData2.data(), 1, addData2.size(), fh);
                        std::fclose(fh);
                    } else {
                        warnings.push_back("Cannot write patched .add for SP params.");
                    }
                }
                // Write patched .am
                {
                    std::FILE* fh = std::fopen(amDest.c_str(), "wb");
                    if (fh) {
                        std::fwrite(amData.data(), 1, amData.size(), fh);
                        std::fclose(fh);
                    } else {
                        warnings.push_back("Cannot write patched .am for SP params.");
                    }
                }
            }
        } else if (!am_read_ok) {
            warnings.push_back("Cannot read dest .am for SP param patch — params not written.");
        }
    }

    // ── Step 8c: patch trigger timing in dest .am JSON blocks ────────────────
    // OpenADS rewrites triggers with timing=0 when it converts SAP binary records
    // to JSON-in-.am format.  Patch the "timing":N field in each trigger's JSON
    // block using the authoritative values read from SAP system.triggers (Step 5e).
    if (!trigger_timings.empty()) {
        // Build (table_lower → (name → timing)) lookup
        struct TimingKey { std::string table_lower, name; };
        std::vector<std::pair<TimingKey, std::uint32_t>> timing_map;
        for (const auto& tt : trigger_timings) {
            std::string tl = tt.table_name;
            for (auto& c : tl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            timing_map.push_back({{tl, tt.name}, tt.timing});
        }

        // Re-derive .am path
        std::string amDest8c;
        {
            auto dot = dest.rfind('.');
            auto sl  = dest.find_last_of("/\\");
            if (dot == std::string::npos || (sl != std::string::npos && dot < sl))
                amDest8c = dest + ".am";
            else
                amDest8c = dest.substr(0, dot) + ".am";
        }

        auto read_u32_8c = [](const std::uint8_t* p) -> std::uint32_t {
            return static_cast<std::uint32_t>(p[0])
                 | (static_cast<std::uint32_t>(p[1]) <<  8)
                 | (static_cast<std::uint32_t>(p[2]) << 16)
                 | (static_cast<std::uint32_t>(p[3]) << 24);
        };

        auto load_file = [](const std::string& path, std::vector<std::uint8_t>& out) -> bool {
            std::FILE* fh = std::fopen(path.c_str(), "rb");
            if (!fh) return false;
            std::fseek(fh, 0, SEEK_END);
            long sz = std::ftell(fh);
            std::rewind(fh);
            if (sz > 0) { out.resize(static_cast<std::size_t>(sz)); [[maybe_unused]] auto _r4 = std::fread(out.data(), 1, out.size(), fh); }
            std::fclose(fh);
            return !out.empty();
        };

        std::vector<std::uint8_t> addData8c, amData8c;
        if (load_file(dest, addData8c) && load_file(amDest8c, amData8c) && addData8c.size() >= 40) {
            std::uint32_t hdr8c = read_u32_8c(addData8c.data() + 0x20);
            std::uint32_t rec8c = read_u32_8c(addData8c.data() + 0x24);
            bool am8c_mod = false;

            if (rec8c > 0 && hdr8c < addData8c.size()) {
                std::uint32_t tot8c = static_cast<std::uint32_t>((addData8c.size() - hdr8c) / rec8c);

                // Build obj_id → table_name_lower map from Table records
                std::unordered_map<std::uint32_t, std::string> id_to_tbl;
                for (std::uint32_t i = 0; i < tot8c; ++i) {
                    const std::uint8_t* b = addData8c.data() + hdr8c + i * rec8c;
                    if (b[0] != 0x04) continue;
                    char tp[11] = {};
                    std::memcpy(tp, b + 13, 10);
                    for (int k = 9; k >= 0 && (tp[k] == ' ' || tp[k] == '\0'); --k) tp[k] = '\0';
                    if (std::strcmp(tp, "Table") != 0 && std::strcmp(tp, "ADSTable") != 0) continue;
                    std::uint32_t oid = read_u32_8c(b + 5);
                    char nm[201] = {};
                    std::memcpy(nm, b + 23, 200);
                    for (int k = 199; k >= 0 && (nm[k] == ' ' || nm[k] == '\0'); --k) nm[k] = '\0';
                    std::string tl = nm;
                    for (auto& c : tl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    id_to_tbl[oid] = tl;
                }

                for (std::uint32_t i = 0; i < tot8c; ++i) {
                    std::uint8_t* b = addData8c.data() + hdr8c + i * rec8c;
                    if (b[0] != 0x04) continue;
                    char tp[11] = {};
                    std::memcpy(tp, b + 13, 10);
                    for (int k = 9; k >= 0 && (tp[k] == ' ' || tp[k] == '\0'); --k) tp[k] = '\0';
                    if (std::strcmp(tp, "Trigger") != 0) continue;

                    std::uint16_t plen = static_cast<std::uint16_t>(b[223] | (b[224] << 8));
                    if (plen < 1 || b[225] != 0x08) continue;  // not OpenADS JSON format

                    std::uint32_t pid = read_u32_8c(b + 9);
                    auto tbl_it = id_to_tbl.find(pid);
                    if (tbl_it == id_to_tbl.end()) continue;
                    const std::string& tbl_lower = tbl_it->second;

                    char nm[201] = {};
                    std::memcpy(nm, b + 23, 200);
                    for (int k = 199; k >= 0 && (nm[k] == ' ' || nm[k] == '\0'); --k) nm[k] = '\0';
                    std::string trig_name = nm;

                    std::uint32_t correct_timing = 0;
                    for (const auto& kv : timing_map) {
                        if (kv.first.table_lower == tbl_lower && kv.first.name == trig_name) {
                            correct_timing = kv.second;
                            break;
                        }
                    }
                    if (correct_timing == 0) continue;

                    std::uint32_t amBlk = read_u32_8c(b + 498);
                    std::uint32_t amLen = read_u32_8c(b + 502);
                    if (amBlk == 0 || amLen == 0) continue;
                    std::size_t amOff = static_cast<std::size_t>(amBlk) * 8;
                    if (amOff + amLen > amData8c.size()) continue;

                    std::string js(reinterpret_cast<char*>(amData8c.data() + amOff), amLen);
                    const std::string key8c = "\"timing\":";
                    std::size_t kp = js.find(key8c);
                    if (kp == std::string::npos) continue;
                    std::size_t vs = kp + key8c.size(), ve = vs;
                    while (ve < js.size() && std::isdigit(static_cast<unsigned char>(js[ve]))) ++ve;
                    std::string old_v = js.substr(vs, ve - vs);
                    std::string new_v = std::to_string(correct_timing);
                    if (old_v == new_v) continue;
                    if (old_v.size() != new_v.size()) {
                        warnings.push_back("trigger timing: digit length mismatch for " + trig_name);
                        continue;
                    }
                    std::memcpy(amData8c.data() + amOff + vs, new_v.data(), new_v.size());
                    am8c_mod = true;
                }

                if (am8c_mod) {
                    std::FILE* fh = std::fopen(amDest8c.c_str(), "wb");
                    if (fh) {
                        std::fwrite(amData8c.data(), 1, amData8c.size(), fh);
                        std::fclose(fh);
                    } else {
                        warnings.push_back("Cannot write patched .am for trigger timings.");
                    }
                }
            }
        }
    }

    emit_result(true, written_memberships, written_perms, written_func_bodies, written_db_props, "", warnings);
    return 0;
}
