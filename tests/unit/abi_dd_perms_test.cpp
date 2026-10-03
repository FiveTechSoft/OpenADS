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
    make_perm_add(dir,
        "USER admin\n"
        "USERPROP admin;prop_1101=adminpw\n"
        "MEMBER admin=DB:Admin\n");  // no TABLEPERM lines

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[8]  = "tbl";
    UNSIGNED8 user[8] = "alice";

    // Regular users cannot grant themselves rights. Configure through a
    // distinct DB:Admin connection, then verify alice sees the stored grant.
    CHECK(AdsDDSetUserTableRights(hConn, tbl, user, 2) == openads::AE_ACCESS_DENIED);
    ADSHANDLE hAdmin = connect_as(dir / "test.add", "admin", "adminpw");
    REQUIRE(hAdmin != 0);
    REQUIRE(AdsDDSetUserTableRights(hAdmin, tbl, user, 2) == 0);
    REQUIRE(AdsDisconnect(hAdmin) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

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
        "USER admin\n"
        "USERPROP admin;prop_1101=adminpw\n"
        "MEMBER admin=DB:Admin\n"
        "GROUP readers\n"
        "MEMBER alice=readers\n"
        "DBPROP prop_5=1\n");

    ADSHANDLE hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

    UNSIGNED8 tbl[] = "tbl";
    UNSIGNED8 user[] = "alice";
    UNSIGNED8 group[] = "readers";

    CHECK(AdsDDGrantPermission(hConn, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                               group, ADS_PERMISSION_READ) == openads::AE_ACCESS_DENIED);
    ADSHANDLE hAdmin = connect_as(dir / "test.add", "admin", "adminpw");
    REQUIRE(hAdmin != 0);
    REQUIRE(AdsDDGrantPermission(hAdmin, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                 group,
                                 ADS_PERMISSION_READ | ADS_PERMISSION_INSERT) == 0);
    REQUIRE(AdsDDGrantPermission(hAdmin, ADS_DD_TABLE_OBJECT, tbl, nullptr,
                                 user, ADS_PERMISSION_INHERIT) == 0);

    REQUIRE(AdsDisconnect(hAdmin) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    hConn = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(hConn != 0);

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

TEST_CASE("Native SQL DD operation rights cannot be disabled and cover join predicate sources") {
    auto dir = fs::temp_directory_path() / "openads_sql_dd_all_sources";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    make_dbf(dir / "hidden.dbf");
    make_dbf(dir / "third.dbf");
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nTABLE hidden=hidden.dbf\nTABLE third=third.dbf\n"
        "USER alice\nUSERPROP alice;prop_1101=pw\nDBPROP prop_5=1\n"
        "TABLEPERM tbl;alice=1\nTABLEPERM hidden;alice=0\nTABLEPERM third;alice=1\n");
    auto connection = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(connection != 0);
    ADSHANDLE statement = 0;
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    REQUIRE(AdsStmtSetTableRights(statement, 0) == 0);
    auto execute = [&](const std::string& text, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(text.begin(), text.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    for (const auto& text : {
            "SELECT * FROM hidden",
            "SELECT * FROM hidden.dbf",
            "SELECT tbl.ID FROM tbl INNER JOIN hidden ON tbl.ID = hidden.ID",
            "SELECT tbl.ID FROM tbl, hidden, third WHERE tbl.ID = hidden.ID AND hidden.ID = third.ID",
            "SELECT ID FROM tbl WHERE EXISTS (SELECT ID FROM hidden)",
            "SELECT ID FROM tbl WHERE ID IN (SELECT ID FROM hidden)",
            "SELECT ID FROM tbl WHERE ID = (SELECT ID FROM hidden)",
            "UPDATE tbl SET ID = 'EDIT'",
            "DELETE FROM tbl",
            "INSERT INTO tbl (ID) VALUES ('EDIT')"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(text));
        CHECK(execute(text, &cursor) == 7200);
        CHECK(cursor == 0);
        UNSIGNED32 code = 0;
        UNSIGNED8 message[2048]{};
        UNSIGNED16 length = sizeof(message);
        REQUIRE(AdsGetLastError(&code, message, &length) == 0);
        CHECK(std::string(reinterpret_cast<char*>(message)).find("NativeError = 7079") != std::string::npos);
    }
    ADSHANDLE cursor = 0;
    REQUIRE(execute("SELECT ID FROM tbl", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native DD SQL schema changes require administrator rather than DML permission") {
    const auto dir = fs::temp_directory_path() / "openads_sql_dd_schema_acl";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nUSER alice\nUSERPROP alice;prop_1101=pw\n"
        "USER admin\nUSERPROP admin;prop_1101=pw\nMEMBER admin=DB:Admin\n"
        "DBPROP prop_5=1\nTABLEPERM tbl;alice=4\n");
    const auto connection = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(connection != 0);
    ADSHANDLE statement = 0;
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    for (const auto& text : {"DROP TABLE tbl", "DROP TABLE tbl.dbf",
            "ALTER TABLE tbl ADD COLUMN EXTRA CHAR(4)",
            "CREATE INDEX secret_idx ON tbl (ID)",
            "DROP INDEX secret_idx ON tbl", "CREATE TABLE created (ID CHAR(4))",
            "CREATE TABLE copied AS SELECT ID FROM tbl",
            "CREATE DATABASE 'created.add'"}) {
        INFO(std::string(text));
        std::vector<UNSIGNED8> sql(text, text + std::strlen(text));
        sql.push_back(0);
        ADSHANDLE cursor = 0;
        CHECK(AdsExecuteSQLDirect(statement, sql.data(), &cursor) == 7200);
        CHECK(cursor == 0);
        UNSIGNED32 code = 0;
        UNSIGNED8 message[2048]{};
        UNSIGNED16 length = sizeof(message);
        REQUIRE(AdsGetLastError(&code, message, &length) == 0);
        CHECK(std::string(reinterpret_cast<char*>(message)).find("NativeError = 7079") != std::string::npos);
        CHECK(fs::exists(dir / "tbl.dbf"));
        CHECK_FALSE(fs::exists(dir / "tbl.cdx"));
        CHECK_FALSE(fs::exists(dir / "created.dbf"));
        CHECK_FALSE(fs::exists(dir / "copied.dbf"));
        CHECK_FALSE(fs::exists(dir / "created.add"));
    }
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    const auto admin = connect_as(dir / "test.add", "admin", "pw");
    REQUIRE(admin != 0);
    REQUIRE(AdsCreateSQLStatement(admin, &statement) == 0);
    UNSIGNED8 create[] = "CREATE TABLE created (ID CHAR(4))";
    ADSHANDLE cursor = 0;
    REQUIRE(AdsExecuteSQLDirect(statement, create, &cursor) == 0);
    CHECK(fs::exists(dir / "created.dbf"));
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(admin) == 0);
    fs::remove_all(dir, error);
}


TEST_CASE("Native SQL column ACLs authorize expressions before materializing") {
    using DD = openads::engine::DataDict;
    const auto dir = fs::temp_directory_path() / "openads_sql_column_expressions";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    make_dbf4(dir / "tbl.dbf");
    {
        std::fstream file(dir / "tbl.dbf", std::ios::binary | std::ios::in | std::ios::out);
        const char count[4] = {1, 0, 0, 0};
        file.seekp(4); file.write(count, 4);
        file.seekp(161); file.write(" ID01SHOWNAMEHIDE", 17);
        file.put(static_cast<char>(0x1a));
    }
    make_dbf4(dir / "other.dbf");
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nTABLE other=other.dbf\n"
        "USER alice\nUSERPROP alice;prop_1101=pw\nDBPROP prop_5=1\n"
        "TABLEPERM tbl;alice=1\nTABLEPERM other;alice=1\n");
    {
        auto result = DD::open((dir / "test.add").string());
        REQUIRE(result.has_value());
        REQUIRE(result.value().grant_column_permission("tbl", "RENT", "alice", DD::DD_PERM_SELECT).has_value());
        REQUIRE(result.value().grant_column_permission("tbl", "TENANT", "alice", DD::DD_PERM_SELECT).has_value());
    }
    const auto connection = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(connection != 0);
    ADSHANDLE statement = 0;
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& text, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(text.begin(), text.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    for (const auto& text : {
            "SELECT DEPOSIT FROM tbl.dbf",
            "SELECT COUNT(DEPOSIT) FROM tbl",
            "SELECT SUM(DEPOSIT) FROM tbl",
            "SELECT UPPER(DEPOSIT) FROM tbl",
            "SELECT DEPOSIT + 1 FROM tbl",
            "SELECT CASE WHEN DEPOSIT = 'hide' THEN 'yes' ELSE 'no' END FROM tbl",
            "SELECT RENT FROM tbl WHERE DEPOSIT = 'hide'",
            "SELECT RENT FROM tbl ORDER BY DEPOSIT",
            "SELECT COUNT(*) FROM tbl GROUP BY DEPOSIT",
            "SELECT COUNT(*) FROM tbl HAVING SUM(DEPOSIT) > 0",
            "SELECT COUNT(*) FILTER (WHERE DEPOSIT = 'hide') FROM tbl",
            "SELECT ROW_NUMBER() OVER (PARTITION BY DEPOSIT ORDER BY RENT) FROM tbl",
            "SELECT tbl.RENT FROM tbl INNER JOIN other ON tbl.RENT = other.RENT",
            "SELECT RENT FROM other WHERE RENT IN (SELECT DEPOSIT FROM tbl)",
            "SELECT RENT FROM tbl WHERE EXISTS (SELECT RENT FROM other)"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(text));
        CHECK(execute(text, &cursor) == 7200);
        CHECK(cursor == 0);
        UNSIGNED32 code = 0;
        UNSIGNED8 message[2048]{};
        UNSIGNED16 length = sizeof(message);
        REQUIRE(AdsGetLastError(&code, message, &length) == 0);
        CHECK(std::string(reinterpret_cast<char*>(message)).find("NativeError = 7079") != std::string::npos);
    }
    for (const auto& text : {"SELECT RENT FROM tbl", "SELECT RENT FROM tbl.dbf",
            "SELECT COUNT(RENT) FROM tbl", "SELECT UPPER(RENT) FROM tbl",
            "SELECT RENT + 1 FROM tbl",
            "SELECT RENT FROM tbl WHERE RENT = 'show' ORDER BY RENT"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(text));
        REQUIRE(execute(text, &cursor) == 0);
        REQUIRE(cursor != 0);
        CHECK(AdsCloseTable(cursor) == 0);
    }
    for (const auto& text : {"SELECT * FROM tbl", "SELECT * FROM tbl.dbf"}) {
        ADSHANDLE cursor = 0;
        REQUIRE(execute(text, &cursor) == 0);
        UNSIGNED16 count = 0;
        REQUIRE(AdsGetNumFields(cursor, &count) == 0);
        CHECK(count == 2);
        REQUIRE(AdsGotoTop(cursor) == 0);
        UNSIGNED8 raw[256]{};
        UNSIGNED32 raw_length = sizeof(raw);
        REQUIRE(AdsGetRecord(cursor, raw, &raw_length) == 0);
        CHECK(std::string(reinterpret_cast<char*>(raw), raw_length).find("HIDE") == std::string::npos);
        UNSIGNED8 forbidden[] = "DEPOSIT", value[32]{};
        UNSIGNED32 value_length = sizeof(value);
        CHECK(AdsGetString(cursor, forbidden, value, &value_length, 0) != 0);
        CHECK(AdsCloseTable(cursor) == 0);
    }
    CHECK(AdsCloseSQLStatement(statement) == 0);
    CHECK(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native DD direct schema APIs deny nonadmin but cursor materialization is private") {
    const auto dir = fs::temp_directory_path() / "openads_dd_native_schema_api";
    std::error_code error;
    fs::remove_all(dir, error); fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    {
        std::fstream file(dir / "tbl.dbf", std::ios::binary | std::ios::in | std::ios::out);
        const char count[4] = {1, 0, 0, 0};
        file.seekp(4); file.write(count, 4);
        file.seekp(65); file.write(" DATA", 5); file.put(static_cast<char>(0x1a));
    }

    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nUSER alice\nUSERPROP alice;prop_1101=pw\n"
        "USER admin\nUSERPROP admin;prop_1101=pw\nMEMBER admin=DB:Admin\n"
        "DBPROP prop_5=1\nTABLEPERM tbl;alice=4\n");
    auto connection = connect_as(dir / "test.add", "alice", "pw");
    REQUIRE(connection != 0);
    UNSIGNED8 name[] = "tbl", created[] = "_srt_user", defs[] = "ID,Character,4";
    ADSHANDLE table = 0;
    CHECK(AdsCreateTable(connection, created, nullptr, ADS_ADT, 0, 0, 0, 0, defs, &table) == 7079);
    CHECK_FALSE(fs::exists(dir / "_srt_user.dbf"));
    CHECK(AdsDropTable(connection, name, 1) == 7079);
    UNSIGNED8 add[] = "EXTRA,Character,4";
    CHECK(AdsRestructureTable(connection, name, nullptr, 0, 0, 0, 0, add, nullptr, nullptr) == 7079);
    REQUIRE(AdsOpenTable(connection, name, nullptr, ADS_CDX, 0, 0, 0, ADS_SHARED, &table) == 0);
    CHECK(AdsPackTable(table) == 7079);
    CHECK(AdsZapTable(table) == 7079);
    CHECK(AdsReindex(table) == 7079);
    UNSIGNED8 bag[] = "tbl.cdx", tag[] = "TEST", expr[] = "ID";
    ADSHANDLE index = 0;
    CHECK(AdsCreateIndex61(table, bag, tag, expr, nullptr, nullptr, ADS_COMPOUND, 512, &index) == 7079);
    CHECK_FALSE(fs::exists(dir / "tbl.cdx"));
    REQUIRE(AdsCloseTable(table) == 0);
    ADSHANDLE statement = 0, cursor = 0;
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    UNSIGNED8 sql[] = "SELECT ID FROM tbl ORDER BY ID";
    REQUIRE(AdsExecuteSQLDirect(statement, sql, &cursor) == 0);
    REQUIRE(cursor != 0);
    CHECK(AdsPackTable(cursor) == 0);
    CHECK(AdsZapTable(cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    CHECK(fs::exists(dir / "tbl.dbf"));
    connection = connect_as(dir / "test.add", "admin", "pw");
    REQUIRE(connection != 0);
    REQUIRE(AdsCreateTable(connection, created, nullptr, ADS_ADT, 0, 0, 0, 0, defs, &table) == 0);
    REQUIRE(AdsCloseTable(table) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native DD row APIs ignore rights bypass and caller alias") {
    const auto dir = fs::temp_directory_path() / "openads_native_row_acl";
    std::error_code error;
    fs::remove_all(dir, error); fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf");
    {
        std::fstream file(dir / "tbl.dbf", std::ios::binary | std::ios::in | std::ios::out);
        const char count[4] = {1, 0, 0, 0};
        file.seekp(4); file.write(count, 4);
        file.seekp(65); file.write(" DATA", 5); file.put(static_cast<char>(0x1a));
    }
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nUSER alice\nUSERPROP alice;prop_1101=pw\n"
        "DBPROP prop_5=1\nTABLEPERM tbl;alice=1\n");
    ADSHANDLE connection = connect_as(dir / "test.add", "alice", "pw"), table = 0;
    REQUIRE(connection != 0);
    UNSIGNED8 name[] = "tbl.dbf", alias[] = "public", field[] = "ID", value[] = "LEAK";
    REQUIRE(AdsOpenTable(connection, name, alias, ADS_CDX, 0, 0, 0, ADS_SHARED, &table) == 0);
    REQUIRE(AdsGotoTop(table) == 0);
    CHECK(AdsSetString(table, field, value, 4) == 7079);
    CHECK(AdsSetLong(table, field, 123) == 7079);
    CHECK(AdsSetFieldRaw(table, field, value, 4) == 7079);
    CHECK(AdsSetRecord(table, value, 4) == 7079);
    CHECK(AdsAppendRecord(table) == 7079);
    CHECK(AdsDeleteRecord(table) == 7079);
    CHECK(AdsRecallRecord(table) == 7079);
    UNSIGNED8 output[32]{}; UNSIGNED32 length = sizeof(output);
    CHECK(AdsGetString(table, field, output, &length, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(output), 4) == "DATA");
    UNSIGNED8 copy[] = "copied.dbf";
    CHECK(AdsCopyTable(table, 0, copy) == 7079);
    CHECK(AdsCopyTableStructure(table, copy) == 7079);
    CHECK_FALSE(fs::exists(dir / "copied.dbf"));
    REQUIRE(AdsCloseTable(table) == 0); REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native DD raw row reads cannot bypass column grants") {
    using DD = openads::engine::DataDict;
    const auto dir = fs::temp_directory_path() / "openads_native_column_acl";
    std::error_code error;
    fs::remove_all(dir, error); fs::create_directories(dir);
    make_dbf4(dir / "tbl.dbf");
    {
        std::fstream file(dir / "tbl.dbf", std::ios::binary | std::ios::in | std::ios::out);
        const char count[4] = {1, 0, 0, 0};
        file.seekp(4); file.write(count, 4);
        file.seekp(161); file.write(" ID01SHOWNAMEHIDE", 17); file.put(static_cast<char>(0x1a));
    }
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nUSER alice\nUSERPROP alice;prop_1101=pw\n"
        "DBPROP prop_5=1\nTABLEPERM tbl;alice=1\n");
    {
        auto dd = DD::open((dir / "test.add").string()); REQUIRE(dd.has_value());
        REQUIRE(dd.value().grant_column_permission("tbl", "RENT", "alice", DD::DD_PERM_SELECT).has_value());
    }
    auto connection = connect_as(dir / "test.add", "alice", "pw"); REQUIRE(connection != 0);
    auto table = open_tbl(connection, 0, ADS_READONLY); REQUIRE(table != 0);
    REQUIRE(AdsGotoTop(table) == 0);
    UNSIGNED8 field[] = "DEPOSIT", allowed[] = "RENT", value[256]{};
    UNSIGNED32 length = sizeof(value);
    CHECK(AdsGetString(table, field, value, &length, 0) == 7079);
    length = sizeof(value); CHECK(AdsGetFieldRaw(table, field, value, &length) == 7079);
    length = sizeof(value); CHECK(AdsGetRecord(table, value, &length) == 7079);
    UNSIGNED32 crc = 0; CHECK(AdsGetRecordCRC(table, &crc, 0) == 7079);
    length = sizeof(value); CHECK(AdsGetString(table, allowed, value, &length, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(value), 4) == "SHOW");
    REQUIRE(AdsCloseTable(table) == 0); REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native DD index deletion and copy destination require authority") {
    const auto dir = fs::temp_directory_path() / "openads_native_index_copy_acl";
    std::error_code error;
    fs::remove_all(dir, error); fs::create_directories(dir);
    make_dbf(dir / "tbl.dbf"); make_dbf(dir / "dst.dbf");
    openads_test::make_dd(dir / "test.add",
        "TABLE tbl=tbl.dbf\nTABLE dst=dst.dbf\nUSER alice\nUSERPROP alice;prop_1101=pw\n"
        "USER admin\nUSERPROP admin;prop_1101=pw\nMEMBER admin=DB:Admin\n"
        "DBPROP prop_5=1\nTABLEPERM tbl;alice=4\nTABLEPERM dst;alice=1\n");
    auto admin = connect_as(dir / "test.add", "admin", "pw"); REQUIRE(admin != 0);
    auto table = open_tbl(admin, 0); REQUIRE(table != 0);
    UNSIGNED8 bag[] = "tbl.cdx", tag[] = "IDTAG", expression[] = "ID";
    ADSHANDLE index = 0;
    REQUIRE(AdsCreateIndex61(table, bag, tag, expression, nullptr, nullptr, ADS_COMPOUND, 512, &index) == 0);
    REQUIRE(AdsCloseTable(table) == 0); REQUIRE(AdsDisconnect(admin) == 0);
    auto alice = connect_as(dir / "test.add", "alice", "pw"); REQUIRE(alice != 0);
    table = open_tbl(alice, 0); REQUIRE(table != 0);
    ADSHANDLE indexes[10]{}; UNSIGNED16 count = 10;
    REQUIRE(AdsOpenIndex(table, bag, indexes, &count) == 0); REQUIRE(count > 0);
    ADSHANDLE created_index = 0;
    CHECK(AdsCreateIndex(table, bag, tag, expression, nullptr, ADS_COMPOUND, ADS_STRING, &created_index) == 7079);
    CHECK(AdsDeleteIndex(indexes[0]) == 7079);
    CHECK(AdsAddCustomKey(indexes[0]) == 7079);
    CHECK(AdsDeleteCustomKey(indexes[0]) == 7079);
    UNSIGNED8 destination[] = "dst"; ADSHANDLE target = 0;
    REQUIRE(AdsOpenTable(alice, destination, nullptr, ADS_CDX, 0, 0, 0, ADS_SHARED, &target) == 0);
    CHECK(AdsCopyTableContents(table, target, 0) == 7079);
    CHECK(AdsCopyTableContent(table, target) == 7079);
    REQUIRE(AdsCloseTable(target) == 0); REQUIRE(AdsCloseTable(table) == 0);
    REQUIRE(AdsDisconnect(alice) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("Native free-table copy rejects paths outside the data root") {
    const auto dir = fs::temp_directory_path() / "openads_native_copy_jail";
    const auto outside = fs::temp_directory_path() / "openads_native_copy_sentinel.dbf";
    std::error_code error;
    fs::remove_all(dir, error); fs::create_directories(dir); make_dbf(dir / "tbl.dbf");
    std::ofstream(outside) << "sentinel";
    auto connection = connect_as(dir, nullptr, nullptr); REQUIRE(connection != 0);
    auto table = open_tbl(connection, 0); REQUIRE(table != 0);
    auto path = outside.string(); std::vector<UNSIGNED8> name(path.begin(), path.end()); name.push_back(0);
    CHECK(AdsCopyTable(table, 0, name.data()) == 7079);
    CHECK(AdsCopyTableStructure(table, name.data()) == 7079);
    std::ifstream input(outside); std::string contents; input >> contents;
    CHECK(contents == "sentinel");
    REQUIRE(AdsCloseTable(table) == 0); REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error); fs::remove(outside, error);
}
