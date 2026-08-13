// abi_dd_remote_phase2_test.cpp -- M12.30: AdsDD* Data Dictionary property
// API over a remote (tcp://) connection, phase 2 (the surface deferred by
// phase 1 / M12.29, see docs/wire-protocol.md Â§5.25): user/group management,
// links, referential integrity create, views, index-file registration,
// index/user-table-rights properties, and permissions.
//
// Same fixture/verification style as abi_dd_remote_test.cpp (phase 1):
// exercise each call over a REMOTE connection and, where a phase-1 or
// phase-2 getter already exists, cross-check the result against what a
// LOCAL connection to the same .add sees.
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

struct DdRemoteFixture {
    fs::path dir;
    std::string add_path;
    Server srv;

    DdRemoteFixture() {
        dir = fs::temp_directory_path() / "openads_dd_remote_p2";
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
    if (std::strcmp(ent, "ri") == 0)
        rc = AdsDDGetRefIntegrityProperty(hConn, nbuf, prop, buf, &len);
    else if (std::strcmp(ent, "view") == 0)
        rc = AdsDDGetViewProperty(hConn, nbuf, prop, buf, &len);
    REQUIRE(rc == 0);
    return std::string(buf, len);
}

} // namespace

TEST_CASE("M12.30 remote AdsDDCreateUser + AdsDDDropObject(User)") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 group[8] = "";
    UNSIGNED8 user[16] = "alice";
    UNSIGNED8 pwd[16]  = "s3cret";
    UNSIGNED8 desc[32] = "test user";

    REQUIRE(AdsDDCreateUser(hRemote, group, user, pwd, desc) == 0);
    // Custom (non-special-cased) property confirms this reached a real
    // DataDict and persisted the description, not the old silent
    // "success, did nothing" over remote.
    char buf[64]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetUserProperty(hRemote, user, 1, buf, &len) == 0);
    CHECK(std::string(buf, len) == "test user");

    REQUIRE(AdsDDDeleteUser(hRemote, user) == 0);
    AdsDisconnect(hRemote);

    ADSHANDLE hLocal = f.connect_local();
    // Persisted to disk: local sees the same (deleted) user state -- the
    // description property is gone along with the user record.
    len = sizeof(buf);
    REQUIRE(AdsDDGetUserProperty(hLocal, user, 1, buf, &len) == 0);
    CHECK(std::string(buf, len).empty());
    AdsDisconnect(hLocal);
}

TEST_CASE("M12.30 remote AdsDDAddUserToGroup / AdsDDRemoveUserFromGroup") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 group[16] = "";
    UNSIGNED8 user[16]  = "bob";
    REQUIRE(AdsDDCreateUser(hRemote, group, user, nullptr, nullptr) == 0);

    UNSIGNED8 grp[16] = "testgrp";
    REQUIRE(AdsDDAddUserToGroup(hRemote, grp, user) == 0);
    REQUIRE(AdsDDRemoveUserFromGroup(hRemote, grp, user) == 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDCreateLink + AdsDDModifyLink + AdsDDDropLink") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 alias[16] = "lnk1";
    UNSIGNED8 path1[260];
    std::memcpy(path1, f.add_path.c_str(), f.add_path.size() + 1);

    REQUIRE(AdsDDCreateLink(hRemote, alias, path1, nullptr, nullptr, 0) == 0);

    UNSIGNED8 path2[32] = "somewhere_else.add";
    REQUIRE(AdsDDModifyLink(hRemote, alias, path2, nullptr, nullptr, 0) == 0);
    REQUIRE(AdsDDDropLink(hRemote, alias, 0) == 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDCreateRefIntegrity + AdsDDDropObject(RefIntegrity) -- "
         "cross-checked against the phase-1 property getter") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 riName[32]    = "ri_self";
    UNSIGNED8 failTbl[16]   = "fail_ri";
    UNSIGNED8 parent[16]    = "stock";
    UNSIGNED8 parentTag[16] = "VAL";
    UNSIGNED8 child[16]     = "stock";
    UNSIGNED8 childTag[16]  = "VAL";

    REQUIRE(AdsDDCreateRefIntegrity(hRemote, riName, failTbl, parent, parentTag,
                                    child, childTag, 1, 1) == 0);
    // Phase-1 DDGetProperty confirms the phase-2 create landed correctly.
    CHECK(get_str(hRemote, "ri", "ri_self", ADS_DD_RI_PARENT) == "stock");
    CHECK(get_str(hRemote, "ri", "ri_self", ADS_DD_RI_FAIL_TABLE) == "fail_ri");

    REQUIRE(AdsDDRemoveRefIntegrity(hRemote, riName) == 0);
    char buf[64]; UNSIGNED16 len = sizeof(buf);
    CHECK(AdsDDGetRefIntegrityProperty(hRemote, riName, ADS_DD_RI_PARENT,
                                       buf, &len) != 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDCreateView -- cross-checked against the "
         "phase-1 property getter") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 vname[32]   = "v_new";
    UNSIGNED8 comment[32] = "created remotely";
    UNSIGNED8 sql[64]     = "SELECT * FROM stock WHERE VAL > 0";

    REQUIRE(AdsDDCreateView(hRemote, vname, comment, sql) == 0);
    CHECK(get_str(hRemote, "view", "v_new", ADS_DD_VIEW_STMT) ==
         "SELECT * FROM stock WHERE VAL > 0");
    CHECK(get_str(hRemote, "view", "v_new", ADS_DD_VIEW_COMMENT) ==
         "created remotely");

    REQUIRE(AdsDDDropView(hRemote, vname) == 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDDropProcedure / AdsDDDropFunction") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 procName[32] = "sp_temp";
    UNSIGNED8 funcName[32] = "fn_temp";

    REQUIRE(AdsDDCreateProcedure(hRemote, procName, nullptr, nullptr, 0,
                                 nullptr, nullptr, nullptr) == 0);
    REQUIRE(AdsDDCreateFunction(hRemote, funcName, nullptr, nullptr,
                                nullptr, nullptr, nullptr) == 0);

    char buf[64]; UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetProcProperty(hRemote, procName, ADS_DD_PROC_CONTAINER,
                                 buf, &len) == 0);
    REQUIRE(AdsDDGetFunctionProperty(hRemote, funcName, 703 /*container*/,
                                     buf, &len) == 0);

    REQUIRE(AdsDDDropProcedure(hRemote, procName) == 0);
    REQUIRE(AdsDDDropFunction(hRemote, funcName) == 0);

    len = sizeof(buf);
    CHECK(AdsDDGetProcProperty(hRemote, procName, ADS_DD_PROC_CONTAINER,
                               buf, &len) != 0);
    len = sizeof(buf);
    CHECK(AdsDDGetFunctionProperty(hRemote, funcName, 703, buf, &len) != 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDAddIndexFile / AdsDDRemoveIndexFile") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 tbl[16] = "stock";
    UNSIGNED8 idx[16] = "stock.adi";
    UNSIGNED8 cmt[16] = "sidecar";

    REQUIRE(AdsDDAddIndexFile(hRemote, tbl, idx, cmt) == 0);
    REQUIRE(AdsDDRemoveIndexFile(hRemote, tbl, idx, 0) == 0);
    AdsDisconnect(hRemote);
}

TEST_CASE("M12.30 remote AdsDDGetIndexProperty -- routes to a real error, "
         "not the old silent empty success") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    ADSHANDLE hLocal  = f.connect_local();

    UNSIGNED8 tbl[16] = "";      // empty: skip the owning-connection filter
    UNSIGNED8 idx[16] = "no_such_tag";
    char buf[64];
    UNSIGNED16 lenRemote = sizeof(buf);
    UNSIGNED32 rcRemote = AdsDDGetIndexProperty(hRemote, tbl, idx,
        ADS_DD_INDEX_EXPRESSION, buf, &lenRemote);
    UNSIGNED16 lenLocal = sizeof(buf);
    UNSIGNED32 rcLocal = AdsDDGetIndexProperty(hLocal, tbl, idx,
        ADS_DD_INDEX_EXPRESSION, buf, &lenLocal);

    CHECK(rcRemote != 0);
    CHECK(rcRemote == rcLocal);
    AdsDisconnect(hRemote);
    AdsDisconnect(hLocal);
}

TEST_CASE("M12.30 remote AdsDDGetUserTableRights / AdsDDSetUserTableRights") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 group[8] = "";
    UNSIGNED8 user[16] = "carol";
    REQUIRE(AdsDDCreateUser(hRemote, group, user, nullptr, nullptr) == 0);

    UNSIGNED8 tbl[16] = "stock";
    REQUIRE(AdsDDSetUserTableRights(hRemote, tbl, user, 3) == 0);

    UNSIGNED32 level = 0;
    REQUIRE(AdsDDGetUserTableRights(hRemote, tbl, user, &level) == 0);
    CHECK(level == 3u);
    AdsDisconnect(hRemote);

    ADSHANDLE hLocal = f.connect_local();
    UNSIGNED32 levelLocal = 0;
    REQUIRE(AdsDDGetUserTableRights(hLocal, tbl, user, &levelLocal) == 0);
    CHECK(levelLocal == 3u);
    AdsDisconnect(hLocal);
}

TEST_CASE("M12.30 remote AdsDDGrantPermission / AdsDDGetPermissions / "
         "AdsDDRevokePermission") {
    DdRemoteFixture f;
    ADSHANDLE hRemote = f.connect_remote();
    UNSIGNED8 group[8]    = "";
    UNSIGNED8 grantee[16] = "dave";
    REQUIRE(AdsDDCreateUser(hRemote, group, grantee, nullptr, nullptr) == 0);

    UNSIGNED8 objName[16] = "stock";
    const UNSIGNED16 kTableType = 1;   // matches dd_type_name_from_code(1) == "Table"
    const UNSIGNED32 kBits = 0x03;     // arbitrary non-zero permission bitmask

    REQUIRE(AdsDDGrantPermission(hRemote, kTableType, objName, nullptr,
                                 grantee, kBits) == 0);

    UNSIGNED32 got = 0;
    REQUIRE(AdsDDGetPermissions(hRemote, grantee, kTableType, objName,
                                nullptr, 0, &got) == 0);
    CHECK(got == kBits);

    // AdsDDRevokePermission is a pure wrapper around GrantPermission(...,0)
    // locally -- this pins that the wrapper's remote path also works now
    // that the function it forwards to is wired.
    REQUIRE(AdsDDRevokePermission(hRemote, kTableType, objName, nullptr,
                                  grantee, 0) == 0);
    got = 1;
    REQUIRE(AdsDDGetPermissions(hRemote, grantee, kTableType, objName,
                                nullptr, 0, &got) == 0);
    CHECK(got == 0u);
    AdsDisconnect(hRemote);
}
