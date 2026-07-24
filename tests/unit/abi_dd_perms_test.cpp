#include "doctest.h"
#include "engine/data_dict.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "test_dd_make.h"

#include <algorithm>  // RCB 07/16/2026: std::find — libstdc++ (Linux CI) does
                      // not pull it in transitively the way libc++/MSVC-STL do
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void make_dbf(const fs::path& p) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> f1{};
    std::strncpy(reinterpret_cast<char*>(f1.data()), "ID", 11);
    f1[11] = 'C'; f1[16] = 4;
    push(f1.data(), f1.size());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

// Build a DD with one table "tbl", one user "alice", one group "readers",
// alice in readers. Login is required.
fs::path make_perm_add(const fs::path& dir,
                        const std::string& extra_perms = {}) {
    auto p = dir / "test.add";
    std::string body =
        "TABLE tbl=tbl.dbf\n"
        "USER alice\n"
        "USERPROP alice;prop_1101=pw\n"
        "USER readers\n"
        "MEMBER alice=readers\n"
        "DBPROP prop_5=1\n"
        + extra_perms;
    openads_test::make_dd(p, body);
    return p;
}

// A DBF with four C(4) columns: ID, RENT, TENANT, DEPOSIT. Used for
// column-level permission enforcement (RCB 07/16/2026).
void make_dbf4(const fs::path& p) {
    const char* cols[] = {"ID", "RENT", "TENANT", "DEPOSIT"};
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    const std::uint16_t hlen = 32 + 32 * 4 + 1;
    hdr[8]  = static_cast<std::uint8_t>(hlen & 0xFF);
    hdr[9]  = static_cast<std::uint8_t>(hlen >> 8);
    hdr[10] = 1 + 4 * 4;   // record len: delete flag + 4 * C(4)
    push(hdr.data(), hdr.size());
    for (const char* cn : cols) {
        std::array<std::uint8_t, 32> f{};
        std::strncpy(reinterpret_cast<char*>(f.data()), cn, 11);
        f[11] = 'C'; f[16] = 4;
        push(f.data(), f.size());
    }
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

// Connect to a DD as a given user.
ADSHANDLE connect_as(const fs::path& add_path,
                      const char* user, const char* pwd) {
    ADSHANDLE h = 0;
    UNSIGNED8 srv[512];
    auto s = add_path.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    UNSIGNED8 ubuf[64]{}, pbuf[64]{};
    if (user) std::strncpy(reinterpret_cast<char*>(ubuf), user, 63);
    if (pwd)  std::strncpy(reinterpret_cast<char*>(pbuf), pwd,  63);
    AdsConnect60(srv, ADS_LOCAL_SERVER,
                 user ? ubuf : nullptr,
                 pwd  ? pbuf : nullptr,
                 0, &h);
    return h;
}

ADSHANDLE open_tbl(ADSHANDLE hConn, UNSIGNED16 check_rights,
                   UNSIGNED16 mode = ADS_SHARED) {
    ADSHANDLE h = 0;
    UNSIGNED8 name[8] = "tbl";
    AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                 check_rights, mode, &h);
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------

TEST_CASE("Perms: no ACL → full access for any authenticated user") {
    auto dir = fs::temp_directory_path() / "openads_perm_noacl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir);   // no TABLEPERM lines

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // usCheckRights=1, no ACL for "tbl" → should succeed
    ADSHANDLE hTbl = open_tbl(hConn, 1);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: level 0 (none) blocks table open") {
    auto dir = fs::temp_directory_path() / "openads_perm_none";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=0\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[8] = "tbl";
    UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                 1, ADS_SHARED, &hTbl);
    CHECK(rc == openads::AE_ACCESS_DENIED);
    CHECK(hTbl == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: level 1 (read) allows readonly open") {
    auto dir = fs::temp_directory_path() / "openads_perm_read";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=1\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // read-only open (mode=ADS_READONLY=3) with check_rights → succeeds
    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[8] = "tbl";
    UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                 1, ADS_READONLY, &hTbl);
    CHECK(rc == 0);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);

    // shared (write) open with check_rights → denied at level 1
    ADSHANDLE hTbl2 = 0;
    UNSIGNED32 rc2 = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                  1, ADS_SHARED, &hTbl2);
    CHECK(rc2 == openads::AE_ACCESS_DENIED);
    CHECK(hTbl2 == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: level 2 (write) allows shared open") {
    auto dir = fs::temp_directory_path() / "openads_perm_write";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=2\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    ADSHANDLE hTbl = open_tbl(hConn, 1, ADS_SHARED);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: group membership grants access") {
    auto dir = fs::temp_directory_path() / "openads_perm_group";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    // alice has no direct perm, but is in "readers" which has level 2
    make_perm_add(dir, "TABLEPERM tbl;readers=2\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    ADSHANDLE hTbl = open_tbl(hConn, 1, ADS_SHARED);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: check_rights=0 bypasses ACL") {
    auto dir = fs::temp_directory_path() / "openads_perm_bypass";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=0\n");  // alice has no access

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // usCheckRights=0 → no enforcement
    ADSHANDLE hTbl = open_tbl(hConn, 0, ADS_SHARED);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: AdsDDSetUserTableRights / AdsDDGetUserTableRights round-trip") {
    auto dir = fs::temp_directory_path() / "openads_perm_api";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir);  // no TABLEPERM lines

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[8]  = "tbl";
    UNSIGNED8 user[8] = "alice";

    // Set alice to level 2.
    REQUIRE(AdsDDSetUserTableRights(hConn, tbl, user, 2) == 0);

    UNSIGNED32 lvl = 99;
    REQUIRE(AdsDDGetUserTableRights(hConn, tbl, user, &lvl) == 0);
    CHECK(lvl == 2u);

    // Now alice can open for write.
    ADSHANDLE hTbl = open_tbl(hConn, 1, ADS_SHARED);
    CHECK(hTbl != 0);
    if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Verify that OpenADS bitmask bit positions match SAP ADS_PERMISSION_* constants:
//   READ=0x01  UPDATE=0x02  EXECUTE=0x04  INHERIT=0x08  INSERT=0x10  DELETE=0x20
// This test pins the bit-level encoding so it is not accidentally regressed.
TEST_CASE("Perms: bitmask bit positions — INSERT is 0x10, DELETE is 0x20") {
    auto dir = fs::temp_directory_path() / "openads_perm_bits";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    // level=3 → bitmask = READ(0x01)|UPDATE(0x02)|INSERT(0x10)|DELETE(0x20) = 0x33
    make_perm_add(dir, "TABLEPERM tbl;alice=3\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // alice can open for shared/write (INSERT+UPDATE present).
    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[8] = "tbl";
    UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                 1, ADS_SHARED, &hTbl);
    CHECK(rc == 0);
    CHECK(hTbl != 0);
    if (hTbl) {
        // Append should succeed at level 3.
        REQUIRE(AdsAppendRecord(hTbl) == 0);
        REQUIRE(AdsCloseTable(hTbl) == 0);
    }

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: AdsDDGetPermissions returns direct or inherited masks") {
    auto dir = fs::temp_directory_path() / "openads_perm_get_effective";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    openads_test::make_dd(
        dir / "test.add",
        "TABLE tbl=tbl.dbf\n"
        "USER alice\n"
        "USERPROP alice;prop_1101=pw\n"
        "GROUP readers\n"
        "MEMBER alice=readers\n"
        "DBPROP prop_5=1\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[] = "tbl";
    UNSIGNED8 user[] = "alice";
    UNSIGNED8 group[] = "readers";

    REQUIRE(AdsDDGrantPermission(hConn, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                 group,
                                 ADS_PERMISSION_READ | ADS_PERMISSION_INSERT) == 0);
    REQUIRE(AdsDDGrantPermission(hConn, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                 user, ADS_PERMISSION_INHERIT) == 0);

    UNSIGNED32 mask = 0;
    REQUIRE(AdsDDGetPermissions(hConn, user, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                0, &mask) == 0);
    CHECK(mask == ADS_PERMISSION_INHERIT);

    mask = 0;
    REQUIRE(AdsDDGetPermissions(hConn, user, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                1, &mask) == 0);
    CHECK((mask & ADS_PERMISSION_INHERIT) != 0);
    CHECK((mask & ADS_PERMISSION_READ) != 0);
    CHECK((mask & ADS_PERMISSION_INSERT) != 0);
    CHECK((mask & ADS_PERMISSION_DELETE) == 0);

    UNSIGNED8 upper_user[] = "ALICE";
    UNSIGNED8 upper_tbl[] = "TBL";
    mask = 0;
    REQUIRE(AdsDDGetPermissions(hConn, upper_user, ADS_DD_TABLE_OBJECT,
                                upper_tbl, nullptr, 1, &mask) == 0);
    CHECK((mask & ADS_PERMISSION_READ) != 0);
    CHECK((mask & ADS_PERMISSION_INSERT) != 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// New tests: check_perm() cache, GRANT SQL, cache invalidation
// ---------------------------------------------------------------------------

TEST_CASE("Perms: check_perm direct user — correct bit resolution") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_direct.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.create_user("alice");
    dd.create_user("bob");
    dd.create_group("editors");
    dd.add_user_to_group("alice", "editors");
    // Grant editors: SELECT + INSERT.
    dd.grant_permission("Table", "invoice", "editors",
                        DD::DD_PERM_SELECT | DD::DD_PERM_INSERT);
    // Grant bob: nothing (level 0).
    dd.grant_permission("Table", "invoice", "bob", 0u);

    // alice (via editors) can SELECT and INSERT but not DELETE.
    CHECK( dd.check_perm("alice", "invoice", DD::DD_PERM_SELECT));
    CHECK( dd.check_perm("alice", "invoice", DD::DD_PERM_INSERT));
    CHECK(!dd.check_perm("alice", "invoice", DD::DD_PERM_DELETE));
    CHECK(!dd.check_perm("alice", "invoice",
                         DD::DD_PERM_SELECT | DD::DD_PERM_DELETE));

    // bob has no bits granted.
    CHECK(!dd.check_perm("bob", "invoice", DD::DD_PERM_SELECT));

    // unknown table — no entry in permissions_ for that object → open access.
    CHECK(dd.check_perm("alice", "nosuchtable", DD::DD_PERM_DELETE));

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_direct.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_direct.am",  ec);
}

TEST_CASE("Perms: protected object denies users with no effective grant") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_no_effective.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.create_user("alice");
    dd.create_user("bob");
    dd.grant_permission("Table", "secure", "alice", DD::DD_PERM_SELECT);

    CHECK(dd.check_perm("alice", "secure", DD::DD_PERM_SELECT));
    CHECK(!dd.check_perm("bob", "secure", DD::DD_PERM_SELECT));

    const auto bob_ops = dd.get_effective_ops("bob", "secure");
    CHECK(!bob_ops.open);
    CHECK(!bob_ops.select_);
    CHECK(!bob_ops.insert_);
    CHECK(!bob_ops.update_);
    CHECK(!bob_ops.delete_);
    CHECK(!bob_ops.execute_);
    CHECK(dd.get_effective_permission("bob", "secure") == 0);

    CHECK(dd.check_perm("bob", "nosuchtable", DD::DD_PERM_DELETE));
    const auto open_ops = dd.get_effective_ops("bob", "nosuchtable");
    CHECK(open_ops.open);
    CHECK(open_ops.select_);
    CHECK(dd.get_effective_permission("bob", "nosuchtable") == 4);

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_no_effective.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_no_effective.am",  ec);
}

TEST_CASE("Perms: AdsSys is DB:Admin and stays otherwise locked") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_adssys.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.grant_permission("Table", "secure", "alice", DD::DD_PERM_SELECT);

    REQUIRE(dd.add_user_to_group("adssys", "DB:Admin").has_value());
    CHECK(dd.is_member_of("adssys", "DB:Admin"));
    CHECK(!dd.add_user_to_group("adssys", "OtherGroup").has_value());
    CHECK(!dd.remove_user_from_group("adssys", "DB:Admin").has_value());

    CHECK(dd.check_perm("adssys", "secure", DD::DD_PERM_SELECT));
    CHECK(dd.check_perm("adssys", "secure", DD::DD_PERM_DELETE));
    const auto ops = dd.get_effective_ops("adssys", "secure");
    CHECK(ops.open);
    CHECK(ops.select_);
    CHECK(ops.delete_);
    CHECK(dd.get_effective_permission("adssys", "secure") == 4);

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_adssys.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_adssys.am",  ec);
}

TEST_CASE("Perms: check_perm FULL access grants all bits") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_full.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.create_user("admin");
    dd.grant_permission("Table", "customers", "admin", DD::DD_PERM_FULL);

    CHECK(dd.check_perm("admin", "customers", DD::DD_PERM_SELECT));
    CHECK(dd.check_perm("admin", "customers", DD::DD_PERM_INSERT));
    CHECK(dd.check_perm("admin", "customers", DD::DD_PERM_UPDATE));
    CHECK(dd.check_perm("admin", "customers", DD::DD_PERM_DELETE));
    CHECK(dd.check_perm("admin", "customers", DD::DD_PERM_EXECUTE));
    CHECK(dd.check_perm("admin", "customers",
                        DD::DD_PERM_SELECT | DD::DD_PERM_DELETE));

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_full.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_full.am",  ec);
}

TEST_CASE("Perms: cache invalidated after grant_permission") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_inval.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.create_user("alice");
    dd.grant_permission("Table", "orders", "alice", DD::DD_PERM_SELECT);

    // Cache is warm for alice after first check.
    CHECK( dd.check_perm("alice", "orders", DD::DD_PERM_SELECT));
    CHECK(!dd.check_perm("alice", "orders", DD::DD_PERM_DELETE));

    // Now add DELETE permission — this must invalidate the cache.
    dd.grant_permission("Table", "orders", "alice",
                        DD::DD_PERM_SELECT | DD::DD_PERM_DELETE);

    // Re-check should reflect the new grant.
    CHECK(dd.check_perm("alice", "orders", DD::DD_PERM_DELETE));

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_inval.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_inval.am",  ec);
}

TEST_CASE("Perms: multi-group union — alice sees OR of all groups") {
    using DD = openads::engine::DataDict;
    auto cr = DD::create((fs::temp_directory_path() / "openads_cp_union.add").string());
    REQUIRE(cr.has_value());
    auto& dd = cr.value();
    dd.create_user("alice");
    dd.create_group("readers");
    dd.create_group("writers");
    dd.add_user_to_group("alice", "readers");
    dd.add_user_to_group("alice", "writers");
    dd.grant_permission("Table", "reports", "readers", DD::DD_PERM_SELECT);
    dd.grant_permission("Table", "reports", "writers",
                        DD::DD_PERM_INSERT | DD::DD_PERM_UPDATE);

    // alice is in both groups → gets union of both bitmasks.
    CHECK(dd.check_perm("alice", "reports", DD::DD_PERM_SELECT));
    CHECK(dd.check_perm("alice", "reports", DD::DD_PERM_INSERT));
    CHECK(dd.check_perm("alice", "reports", DD::DD_PERM_UPDATE));
    CHECK(!dd.check_perm("alice", "reports", DD::DD_PERM_DELETE));

    std::error_code ec;
    fs::remove(fs::temp_directory_path() / "openads_cp_union.add", ec);
    fs::remove(fs::temp_directory_path() / "openads_cp_union.am",  ec);
}

TEST_CASE("Perms: GRANT SQL builds bitmask and enforces via AdsOpenTable") {
    auto dir = fs::temp_directory_path() / "openads_perm_grant_sql";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");

    // DD with alice and no initial permission.
    make_perm_add(dir, "TABLEPERM tbl;alice=0\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // alice has no access yet.
    {
        ADSHANDLE hTbl = open_tbl(hConn, 1, ADS_SHARED);
        CHECK(hTbl == 0);
    }

    // Execute GRANT SELECT ON tbl TO alice.
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 grant_sql[] = "GRANT SELECT ON tbl TO alice";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, grant_sql, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);

    // alice can now open read-only.
    {
        UNSIGNED8 name[8] = "tbl";
        ADSHANDLE hTbl = 0;
        UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                     1, ADS_READONLY, &hTbl);
        CHECK(rc == 0);
        CHECK(hTbl != 0);
        if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);
    }

    // But not for shared/write (only SELECT was granted).
    {
        UNSIGNED8 name[8] = "tbl";
        ADSHANDLE hTbl = 0;
        UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                     1, ADS_SHARED, &hTbl);
        CHECK(rc == openads::AE_ACCESS_DENIED);
        if (hTbl) AdsCloseTable(hTbl);
    }

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: REVOKE SQL removes specific bit") {
    auto dir = fs::temp_directory_path() / "openads_perm_revoke_sql";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");

    // Start with full write access.
    make_perm_add(dir, "TABLEPERM tbl;alice=3\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    // Revoke INSERT.
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 rev_sql[] = "REVOKE INSERT ON tbl FROM alice";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, rev_sql, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);

    // SELECT should still work (level 3 had SELECT).
    {
        UNSIGNED8 name[8] = "tbl";
        ADSHANDLE hTbl = 0;
        UNSIGNED32 rc = AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0,
                                     1, ADS_READONLY, &hTbl);
        CHECK(rc == 0);
        if (hTbl) REQUIRE(AdsCloseTable(hTbl) == 0);
    }

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: AdsDDGetTableProperty returns effective level") {
    auto dir = fs::temp_directory_path() / "openads_perm_prop216";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=3\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[8] = "tbl";
    UNSIGNED16 buf = 0;
    UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsDDGetTableProperty(hConn, tbl,
                                  ADS_DD_TABLE_PERMISSION_LEVEL,
                                  &buf, &len) == 0);
    CHECK(buf == 3u);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Perms: level 0 blocks table metadata visibility") {
    auto dir = fs::temp_directory_path() / "openads_perm_metadata_none";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_perm_add(dir, "TABLEPERM tbl;alice=0\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[8] = "tbl";
    UNSIGNED16 buf = 0;
    UNSIGNED16 len = sizeof(buf);
    CHECK(AdsDDGetTableProperty(hConn, tbl,
                                ADS_DD_TABLE_PERMISSION_LEVEL,
                                &buf, &len) == openads::AE_ACCESS_DENIED);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 sql[] = "SELECT Name FROM system.tables";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(hCur != 0);
    UNSIGNED32 count = 99;
    REQUIRE(AdsGetRecordCount(hCur, 0, &count) == 0);
    CHECK(count == 0u);
    REQUIRE(AdsCloseTable(hCur) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// RCB 07/16/2026: end-to-end column-level SELECT enforcement. SAP grants a
// group table access but restricts it to specific columns; OpenADS used to
// leak the whole table. Now: SELECT * exposes only permitted columns, and an
// explicit forbidden column is denied.
TEST_CASE("Perms: column-level SELECT enforcement") {
    using DD = openads::engine::DataDict;
    auto dir = fs::temp_directory_path() / "openads_colperm_enf";
    fs::remove_all(dir);
    fs::create_directories(dir);
    make_dbf4(dir / "tbl.dbf");

    auto add = dir / "test.add";
    openads_test::make_dd(add,
        "TABLE tbl=tbl.dbf\n"
        "USER bob\n"
        "USERPROP bob;prop_1101=pw\n"
        "GROUP General\n"
        "MEMBER bob=General\n"
        "DBPROP prop_5=1\n"           // logins required
        "TABLEPERM tbl;General=1\n"); // table-level SELECT for General

    // General may SELECT only RENT and TENANT (not ID, not DEPOSIT).
    {
        auto opened = DD::open(add.string());
        REQUIRE(opened.has_value());
        DD dd = std::move(opened).value();
        REQUIRE(dd.grant_column_permission("tbl", "RENT",   "General",
                    DD::DD_PERM_SELECT).has_value());
        REQUIRE(dd.grant_column_permission("tbl", "TENANT", "General",
                    DD::DD_PERM_SELECT).has_value());
    }

    ADSHANDLE hConn = connect_as(add, "bob", "pw");
    REQUIRE(hConn != 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // SELECT * → only the two permitted columns are exposed.
    {
        UNSIGNED8 sql[64] = "SELECT * FROM tbl";
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hc) == 0);
        UNSIGNED16 nf = 0;
        REQUIRE(AdsGetNumFields(hc, &nf) == 0);
        CHECK(nf == 2);                 // RENT + TENANT, NOT ID/DEPOSIT
        std::vector<std::string> names;
        for (UNSIGNED16 i = 1; i <= nf; ++i) {
            UNSIGNED8 nm[64] = {0};
            UNSIGNED16 cap = sizeof(nm);
            AdsGetFieldName(hc, i, nm, &cap);
            names.emplace_back(reinterpret_cast<char*>(nm), cap);
        }
        CHECK(std::find(names.begin(), names.end(), "RENT")    != names.end());
        CHECK(std::find(names.begin(), names.end(), "TENANT")  != names.end());
        CHECK(std::find(names.begin(), names.end(), "DEPOSIT") == names.end());
        CHECK(std::find(names.begin(), names.end(), "ID")      == names.end());
        AdsCloseTable(hc);
    }

    // Explicit forbidden column → access denied. Since the S4 AQE
    // envelope, every client-facing SQL failure returns 7200 (SAP
    // semantics) with the native code inside the message text.
    {
        UNSIGNED8 sql[64] = "SELECT DEPOSIT FROM tbl";
        ADSHANDLE hc = 0;
        UNSIGNED32 rc = AdsExecuteSQLDirect(hStmt, sql, &hc);
        CHECK(rc == 7200u);
        if (hc) AdsCloseTable(hc);
    }

    // Explicit permitted column → allowed.
    {
        UNSIGNED8 sql[64] = "SELECT RENT FROM tbl";
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hc) == 0);
        UNSIGNED16 nf = 0;
        REQUIRE(AdsGetNumFields(hc, &nf) == 0);
        CHECK(nf == 1);
        AdsCloseTable(hc);
    }

    // (adssys/DB:Admin bypass is covered by the DataDict unit test.)

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir);
}
