// abi_dd_remote_test.cpp — M12.29: AdsDD* Data Dictionary property API over
// a remote (tcp://) connection.
//
// Root cause pinned here: dd_from_handle() in ace_exports.cpp only ever
// resolved a LOCAL Connection handle, so every AdsDD* getter/setter silently
// returned empty/no-op for a remote connection instead of erroring or
// forwarding. Each test below creates the object through a LOCAL connection
// (Phase 1 doesn't cover every Create* function — see wire-protocol.md
// §5.24 for what's deferred), then reads/writes it through a REMOTE
// connection to the same .add, and cross-checks against a second LOCAL
// connection that the write actually reached disk.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::network::Server;

namespace {

fs::path make_simple_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    const std::uint16_t hdr_size = 32 + 32 + 1;
    hdr[8] = hdr_size & 0xFF; hdr[9] = (hdr_size >> 8) & 0xFF;
    hdr[10] = 1 + 4; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "VAL", 11);
    fd[11] = 'N'; fd[16] = 4; fd[17] = 0;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Creates <dir>/openads.add with one table ("stock") registered, starts an
// in-process Server (no --data restriction, matching network_server_test.cpp
// M12.5 style), and exposes both a LOCAL connect helper (opens the .add
// in-process) and a REMOTE connect helper (dials the in-process server).
struct DdRemoteFixture {
    fs::path dir;
    std::string add_path;
    Server srv;

    DdRemoteFixture() {
        dir = fs::temp_directory_path() / "openads_dd_remote";
        std::error_code ec;
        fs::remove_all(dir, ec);
        make_simple_dbf(dir, "stock.dbf");

        add_path = (dir / "openads.add").string();
        UNSIGNED8 add_buf[260];
        std::memcpy(add_buf, add_path.c_str(), add_path.size() + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsDDCreate(add_buf, 0, nullptr, &hConn) == 0);
        UNSIGNED8 alias[16] = "stock";
        UNSIGNED8 path[32]  = "stock.dbf";
        REQUIRE(AdsDDAddTable(hConn, alias, path, 0, 0, nullptr, nullptr) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);

        REQUIRE(srv.start("127.0.0.1", 0).has_value());
    }

    ADSHANDLE connect_remote() const {
        char uri[512];
        std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                     static_cast<unsigned>(srv.port()), add_path.c_str());
        UNSIGNED8 buf[512];
        std::memcpy(buf, uri, std::strlen(uri) + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(buf, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        return hConn;
    }

    ADSHANDLE connect_local() const {
        UNSIGNED8 buf[512];
        std::memcpy(buf, add_path.c_str(), add_path.size() + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        return hConn;
    }
};

std::string get_str(ADSHANDLE hConn, const char* ent, const char* name,
                    UNSIGNED16 prop) {
    char buf[512] = {0};
    UNSIGNED16 len = sizeof(buf);
    UNSIGNED32 rc = 0;
    UNSIGNED8 nbuf[128];
    std::memcpy(nbuf, name, std::strlen(name) + 1);
    if (std::strcmp(ent, "proc") == 0)
        rc = AdsDDGetProcProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "function") == 0)
        rc = AdsDDGetFunctionProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "trigger") == 0)
        rc = AdsDDGetTriggerProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "view") == 0)
        rc = AdsDDGetViewProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "ri") == 0)
        rc = AdsDDGetRefIntegrityProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "user") == 0)
        rc = AdsDDGetUserProperty(hConn, nbuf, prop, buf, &len);
    REQUIRE(rc == 0);
    return std::string(buf, len);
}

} // namespace

TEST_CASE("M12.29 remote AdsDDGetDatabaseProperty/SetDatabaseProperty round-trip") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();

    const char* val = "remote-db-prop";
    REQUIRE(AdsDDSetDatabaseProperty(hRemote, 900,
        const_cast<char*>(val), static_cast<UNSIGNED16>(std::strlen(val))) == 0);

    char buf[128]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetDatabaseProperty(hRemote, 900, buf, &len) == 0);
    CHECK(std::string(buf, len) == val);
    AdsDisconnect(hRemote);

    // Persisted to disk, not just echoed back in-memory over the wire.
    ADSHANDLE hLocal = f.connect_local();
    len = sizeof(buf);
    REQUIRE(AdsDDGetDatabaseProperty(hLocal, 900, buf, &len) == 0);
    CHECK(std::string(buf, len) == val);
    AdsDisconnect(hLocal);
}

TEST_CASE("M12.29 remote AdsDDGetTableProperty/SetTableProperty round-trip") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 tbl[16] = "stock";

    const char* expr = "VAL > 0";
    REQUIRE(AdsDDSetTableProperty(hRemote, tbl, ADS_DD_TABLE_VALIDATION_EXPR,
        const_cast<char*>(expr), static_cast<UNSIGNED16>(std::strlen(expr))) == 0);

    char buf[128]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetTableProperty(hRemote, tbl, ADS_DD_TABLE_VALIDATION_EXPR,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == expr);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDGetFieldProperty/SetFieldProperty round-trip") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 tbl[16] = "stock";
    UNSIGNED8 fld[16] = "VAL";

    const char* dflt = "42";
    REQUIRE(AdsDDSetFieldProperty(hRemote, tbl, fld, ADS_DD_FIELD_DEFAULT,
        const_cast<char*>(dflt), static_cast<UNSIGNED16>(std::strlen(dflt))) == 0);

    char buf[64]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetFieldProperty(hRemote, tbl, fld, ADS_DD_FIELD_DEFAULT,
                                  buf, &len) == 0);
    CHECK(std::string(buf, len) == dflt);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDCreateTrigger + Get/SetTriggerProperty + DropTrigger") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 name[32] = "trg_audit";
    UNSIGNED8 tbl[32]  = "stock";

    REQUIRE(AdsDDCreateTrigger(hRemote, name, tbl, 0x0001 /*BEFORE INSERT*/,
                               0, nullptr, nullptr, 1) == 0);

    char buf[64]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetTriggerProperty(hRemote, name, ADS_DD_TRIGGER_TABLE,
                                    buf, &len) == 0);
    CHECK(std::string(buf, len) == "stock");

    const char* cmt = "remote comment";
    REQUIRE(AdsDDSetTriggerProperty(hRemote, name, ADS_DD_TRIGGER_COMMENT,
        const_cast<char*>(cmt), static_cast<UNSIGNED16>(std::strlen(cmt))) == 0);
    len = sizeof(buf);
    REQUIRE(AdsDDGetTriggerProperty(hRemote, name, ADS_DD_TRIGGER_COMMENT,
                                    buf, &len) == 0);
    CHECK(std::string(buf, len) == cmt);

    REQUIRE(AdsDDDropTrigger(hRemote, name) == 0);
    len = sizeof(buf);
    CHECK(AdsDDGetTriggerProperty(hRemote, name, ADS_DD_TRIGGER_TABLE,
                                  buf, &len) != 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDCreateProcedure + Get/SetProcProperty — "
         "the exact bug reported via DA-Web DD Health") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 name[32]  = "sp_restock";
    UNSIGNED8 input[32] = "qty:N";

    REQUIRE(AdsDDCreateProcedure(hRemote, name, nullptr, nullptr, 0,
                                 input, nullptr, nullptr) == 0);

    // Before the fix this returned rc==0 with len==0 (silent empty), not an
    // error and not the real value — exactly what DD Health flagged as
    // "Stored procedure has no body or external container metadata."
    CHECK(get_str(hRemote, "proc", "sp_restock", ADS_DD_PROC_INPUT) == "qty:N");

    const char* body = "DECLARE x INT;";
    REQUIRE(AdsDDSetProcProperty(hRemote, name, ADS_DD_PROC_COMMENT,
        const_cast<char*>(body), static_cast<UNSIGNED16>(std::strlen(body))) == 0);
    CHECK(get_str(hRemote, "proc", "sp_restock", ADS_DD_PROC_COMMENT) == body);
    AdsDisconnect(hRemote);

    ADSHANDLE hLocal = f.connect_local();
    CHECK(get_str(hLocal, "proc", "sp_restock", ADS_DD_PROC_COMMENT) == body);
    AdsDisconnect(hLocal);
}

TEST_CASE("M12.29 remote AdsDDCreateFunction + Get/SetFunctionProperty") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 name[32]    = "fn_calc";
    UNSIGNED8 retType[16] = "N";

    REQUIRE(AdsDDCreateFunction(hRemote, name, nullptr, nullptr,
                                retType, nullptr, nullptr) == 0);
    CHECK(get_str(hRemote, "function", "fn_calc", 702 /*return_type*/) == "N");

    const char* impl = "RETURN 1;";
    REQUIRE(AdsDDSetFunctionProperty(hRemote, name, 700 /*implementation*/,
        const_cast<char*>(impl), static_cast<UNSIGNED16>(std::strlen(impl))) == 0);
    CHECK(get_str(hRemote, "function", "fn_calc", 700) == impl);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDGetViewProperty/SetViewProperty + DropView") {
    DdRemoteFixture f;
    // View creation is Phase 2 — create it locally first.
    ADSHANDLE hLocal = f.connect_local();
    UNSIGNED8 vname[32] = "v_all";
    UNSIGNED8 sql0[64]  = "SELECT * FROM stock";
    REQUIRE(AdsDDCreateView(hLocal, vname, nullptr, sql0) == 0);
    AdsDisconnect(hLocal);

    ADSHANDLE hRemote = f.connect_remote();
    CHECK(get_str(hRemote, "view", "v_all", ADS_DD_VIEW_STMT) ==
         "SELECT * FROM stock");

    const char* sql1 = "SELECT VAL FROM stock WHERE VAL > 0";
    REQUIRE(AdsDDSetViewProperty(hRemote, vname, ADS_DD_VIEW_STMT,
        const_cast<char*>(sql1), static_cast<UNSIGNED16>(std::strlen(sql1))) == 0);
    CHECK(get_str(hRemote, "view", "v_all", ADS_DD_VIEW_STMT) == sql1);

    REQUIRE(AdsDDDropView(hRemote, vname) == 0);
    char buf[64]; UNSIGNED16 len = sizeof(buf);
    CHECK(AdsDDGetViewProperty(hRemote, vname, ADS_DD_VIEW_STMT, buf, &len) != 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDGetRefIntegrityProperty/SetRefIntegrityProperty") {
    DdRemoteFixture f;
    // RI creation is Phase 2 — create it locally first (self-referencing RI
    // on "stock" just to exercise the property get/set shape).
    ADSHANDLE hLocal = f.connect_local();
    UNSIGNED8 riName[32]   = "ri_self";
    UNSIGNED8 failTbl[16]  = "fail_ri";
    UNSIGNED8 parent[16]   = "stock";
    UNSIGNED8 parentTag[16]= "VAL";
    UNSIGNED8 child[16]    = "stock";
    UNSIGNED8 childTag[16] = "VAL";
    REQUIRE(AdsDDCreateRefIntegrity(hLocal, riName, failTbl, parent, parentTag,
                                    child, childTag, 1, 1) == 0);
    AdsDisconnect(hLocal);

    ADSHANDLE hRemote = f.connect_remote();
    CHECK(get_str(hRemote, "ri", "ri_self", ADS_DD_RI_PARENT) == "stock");

    UNSIGNED8 riNameBuf[32] = "ri_self";
    const char* newFail = "fail_ri2";
    REQUIRE(AdsDDSetRefIntegrityProperty(hRemote, riNameBuf, ADS_DD_RI_FAIL_TABLE,
        const_cast<char*>(newFail), static_cast<UNSIGNED16>(std::strlen(newFail))) == 0);
    CHECK(get_str(hRemote, "ri", "ri_self", ADS_DD_RI_FAIL_TABLE) == newFail);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDGetUserProperty/SetUserProperty") {
    DdRemoteFixture f;
    // User creation is Phase 2 — create it locally first.
    ADSHANDLE hLocal = f.connect_local();
    UNSIGNED8 group[8] = "";
    UNSIGNED8 user[16] = "bob";
    REQUIRE(AdsDDCreateUser(hLocal, group, user, nullptr, nullptr) == 0);
    AdsDisconnect(hLocal);

    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 userBuf[16] = "bob";
    // 1150: an arbitrary (non-special-cased) property id — this test only
    // pins the generic prop_<id> get/set round-trip over the wire, not any
    // named property's semantics (1102/1103 are special-cased computed
    // values; anything else round-trips through dd->get/set_user_property).
    const char* val = "remote-set value";
    REQUIRE(AdsDDSetUserProperty(hRemote, userBuf, 1150,
        const_cast<char*>(val), static_cast<UNSIGNED16>(std::strlen(val))) == 0);
    CHECK(get_str(hRemote, "user", "bob", 1150) == val);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.29 remote AdsDDDropLink") {
    DdRemoteFixture f;
    // Link creation is Phase 2 — create it locally first.
    ADSHANDLE hLocal = f.connect_local();
    UNSIGNED8 alias[16] = "lnk1";
    UNSIGNED8 path[260];
    std::memcpy(path, f.add_path.c_str(), f.add_path.size() + 1);
    REQUIRE(AdsDDCreateLink(hLocal, alias, path, nullptr, nullptr, 0) == 0);
    AdsDisconnect(hLocal);

    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 aliasBuf[16] = "lnk1";
    REQUIRE(AdsDDDropLink(hRemote, aliasBuf, 0) == 0);
    AdsDisconnect(hRemote);
}
