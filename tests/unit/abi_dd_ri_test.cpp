#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "test_dd_make.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Write a minimal 2-field DBF: ID C(4) + NAME C(8).
// All records are live (no deleted flag).
fs::path make_ri_dbf(const fs::path& dir, const char* leaf,
                     const std::vector<std::string>& id_values) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);

    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };

    // Header: 2 fields, each record = 1 + 4 + 8 = 13 bytes
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(id_values.size());
    hdr[8]  = 32 + 32 + 32 + 1;          // header length (3 field descs)
    hdr[9]  = 0;
    hdr[10] = 1 + 4 + 8;                  // record length
    push(hdr.data(), hdr.size());

    // Field 1: ID C(4)
    std::array<std::uint8_t, 32> f1{};
    std::strncpy(reinterpret_cast<char*>(f1.data()), "ID", 11);
    f1[11] = 'C'; f1[16] = 4;
    push(f1.data(), f1.size());

    // Field 2: NAME C(8)
    std::array<std::uint8_t, 32> f2{};
    std::strncpy(reinterpret_cast<char*>(f2.data()), "NAME", 11);
    f2[11] = 'C'; f2[16] = 8;
    push(f2.data(), f2.size());

    file.push_back(0x0D);  // header terminator

    for (auto& id : id_values) {
        file.push_back(' ');   // not deleted
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)id.size()
                           ? static_cast<std::uint8_t>(id[i]) : ' ');
        for (int i = 0; i < 8; ++i)
            file.push_back(' ');   // NAME blank
    }
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Create a minimal Data Dictionary with parent/child tables.
fs::path make_add(const fs::path& dir,
                  const std::string& extra_lines = {}) {
    auto p = dir / "test.add";
    std::string body =
        "TABLE parent=parent.dbf\n"
        "TABLE child=child.dbf\n"
        + extra_lines;
    openads_test::make_dd(p, body);
    return p;
}

// Connect to the .add Data Dictionary.
ADSHANDLE connect_dd(const fs::path& add_path) {
    ADSHANDLE h = 0;
    UNSIGNED8 srv[512];
    auto s = add_path.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h);
    return h;
}

// Open a table by alias through the DD connection.
ADSHANDLE open_alias(ADSHANDLE hConn, const char* alias) {
    ADSHANDLE h = 0;
    UNSIGNED8 name[32];
    std::strncpy(reinterpret_cast<char*>(name), alias,
                 sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0, 0, 0, &h);
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("RI INSERT: valid FK accepted") {
    auto dir = fs::temp_directory_path() / "openads_ri_insert_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"A001", "A002"});
    make_ri_dbf(dir, "child.dbf",  {});
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");   // RESTRICT/RESTRICT

    ADSHANDLE hConn = connect_dd(dir / "test.add");
    REQUIRE(hConn != 0);

    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    // Insert a child row whose FK exists in parent → should succeed.
    REQUIRE(AdsAppendRecord(hChild) == 0);
    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 val[8] = "A001";
    REQUIRE(AdsSetString(hChild, fld, val, 4) == 0);
    CHECK(AdsWriteRecord(hChild) == 0);   // FK "A001" exists

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI INSERT: invalid FK rejected with AE_RI_VIOLATION") {
    auto dir = fs::temp_directory_path() / "openads_ri_insert_bad";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"A001"});
    make_ri_dbf(dir, "child.dbf",  {});
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hConn != 0);
    REQUIRE(hChild != 0);

    REQUIRE(AdsAppendRecord(hChild) == 0);
    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 bad[8] = "ZZZZ";
    REQUIRE(AdsSetString(hChild, fld, bad, 4) == 0);
    UNSIGNED32 rc = AdsWriteRecord(hChild);
    CHECK(rc == openads::AE_RI_VIOLATION);  // "ZZZZ" not in parent

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI INSERT: blank FK is not checked (NULL semantics)") {
    auto dir = fs::temp_directory_path() / "openads_ri_insert_null";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"A001"});
    make_ri_dbf(dir, "child.dbf",  {});
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hChild != 0);

    // Append without setting ID → ID stays blank → no RI check.
    REQUIRE(AdsAppendRecord(hChild) == 0);
    CHECK(AdsWriteRecord(hChild) == 0);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI DELETE RESTRICT: blocked when child rows exist") {
    auto dir = fs::temp_directory_path() / "openads_ri_del_restrict";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});   // child references parent
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");  // RESTRICT on delete

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);

    // Delete parent row that has a child — should be rejected.
    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    REQUIRE(AdsLockTable(hParent) == 0);
    UNSIGNED32 rc = AdsDeleteRecord(hParent);
    CHECK(rc == openads::AE_RI_VIOLATION);

    // Parent row must still be live.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hParent, 0, &cnt) == 0);
    CHECK(cnt == 1u);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI DELETE RESTRICT: allowed when no children exist") {
    auto dir = fs::temp_directory_path() / "openads_ri_del_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001", "P002"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});   // only P001 referenced
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);

    // Delete P002 — no child references it.
    REQUIRE(AdsGotoRecord(hParent, 2) == 0);
    REQUIRE(AdsLockTable(hParent) == 0);
    CHECK(AdsDeleteRecord(hParent) == 0);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI DELETE CASCADE: child rows deleted with parent") {
    auto dir = fs::temp_directory_path() / "openads_ri_del_cascade";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001"});
    make_ri_dbf(dir, "child.dbf",  {"P001", "P001"});  // two child rows
    make_add(dir, "RI r1=parent;child;ID;2;1;\n");  // delete = CASCADE

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    REQUIRE(AdsLockTable(hParent) == 0);
    CHECK(AdsDeleteRecord(hParent) == 0);

    // Both child rows must now be marked deleted.
    UNSIGNED32 live_cnt = 0;
    REQUIRE(AdsGetRecordCount(hChild, 0, &live_cnt) == 0);
    // GetRecordCount with bFilterOption=0 returns all records including deleted.
    // Verify by scanning live records via AdsGetRecordCount with bFilterOption=1
    // is not implemented — check total count still 2 but both deleted.
    CHECK(live_cnt == 2u);  // raw count includes deleted
    REQUIRE(AdsGotoRecord(hChild, 1) == 0);
    UNSIGNED16 del1 = 0;
    REQUIRE(AdsIsRecordDeleted(hChild, &del1) == 0);
    CHECK(del1 == 1);
    REQUIRE(AdsGotoRecord(hChild, 2) == 0);
    UNSIGNED16 del2 = 0;
    REQUIRE(AdsIsRecordDeleted(hChild, &del2) == 0);
    CHECK(del2 == 1);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI DELETE SETNULL: child FK blanked when parent deleted") {
    auto dir = fs::temp_directory_path() / "openads_ri_del_setnull";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});
    make_add(dir, "RI r1=parent;child;ID;2;3;\n");  // delete = SETNULL

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    REQUIRE(AdsLockTable(hParent) == 0);
    CHECK(AdsDeleteRecord(hParent) == 0);

    // Child row must still be live but FK must be blank.
    REQUIRE(AdsGotoRecord(hChild, 1) == 0);
    UNSIGNED16 del = 0;
    REQUIRE(AdsIsRecordDeleted(hChild, &del) == 0);
    CHECK(del == 0);

    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hChild, fld, buf, &cap, 0) == 0);
    bool all_blank = true;
    for (UNSIGNED32 i = 0; i < cap; ++i)
        if (buf[i] != ' ') { all_blank = false; break; }
    CHECK(all_blank);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// UPDATE enforcement tests
// ---------------------------------------------------------------------------

// Helper: set the ID field of the current record and write.
static UNSIGNED32 set_id_and_write(ADSHANDLE h, const char* id) {
    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 val[8] = {};
    std::memcpy(val, id, 4);
    // Lock the table (file lock) — simpler than record lock for RI callers.
    if (UNSIGNED32 rc = AdsLockTable(h); rc != 0) return rc;
    if (UNSIGNED32 rc = AdsSetString(h, fld, val, 4); rc != 0) return rc;
    return AdsWriteRecord(h);
}

TEST_CASE("RI UPDATE RESTRICT: blocked when child rows reference old PK") {
    auto dir = fs::temp_directory_path() / "openads_ri_upd_restrict";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001", "P002"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});   // child references P001
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");  // update=RESTRICT

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hConn   != 0);
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    // Try to change P001 → P999 while a child row still references P001.
    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    UNSIGNED32 rc = set_id_and_write(hParent, "P999");
    CHECK(rc == openads::AE_RI_VIOLATION);

    // Record must be unchanged on disk.
    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED8 fld[4] = "ID";
    REQUIRE(AdsGetField(hParent, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), 4) == "P001");

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI UPDATE RESTRICT: allowed when no child references old PK") {
    auto dir = fs::temp_directory_path() / "openads_ri_upd_restrict_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001", "P002"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});   // P002 is unreferenced
    make_add(dir, "RI r1=parent;child;ID;2;2;\n");

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);

    // P002 has no child rows — rename is allowed.
    REQUIRE(AdsGotoRecord(hParent, 2) == 0);
    CHECK(set_id_and_write(hParent, "P999") == 0);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI UPDATE CASCADE: child FK updated to new parent PK") {
    auto dir = fs::temp_directory_path() / "openads_ri_upd_cascade";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001"});
    make_ri_dbf(dir, "child.dbf",  {"P001", "P001"});   // two child rows
    make_add(dir, "RI r1=parent;child;ID;1;2;\n");  // update=CASCADE

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    // Rename P001 → P999; both child rows should be updated.
    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    CHECK(set_id_and_write(hParent, "P999") == 0);

    // Check both child FK fields were cascaded.
    UNSIGNED8 fld[4] = "ID";
    for (UNSIGNED32 rec = 1; rec <= 2; ++rec) {
        REQUIRE(AdsGotoRecord(hChild, rec) == 0);
        UNSIGNED8 buf[8] = {};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hChild, fld, buf, &cap, 0) == 0);
        CHECK(std::string(reinterpret_cast<char*>(buf), 4) == "P999");
    }

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI UPDATE SETNULL: child FK blanked when parent PK changes") {
    auto dir = fs::temp_directory_path() / "openads_ri_upd_setnull";
    std::error_code ec;
    fs::remove_all(dir, ec);

    make_ri_dbf(dir, "parent.dbf", {"P001"});
    make_ri_dbf(dir, "child.dbf",  {"P001"});
    make_add(dir, "RI r1=parent;child;ID;3;2;\n");  // update=SETNULL

    ADSHANDLE hConn   = connect_dd(dir / "test.add");
    ADSHANDLE hParent = open_alias(hConn, "parent");
    ADSHANDLE hChild  = open_alias(hConn, "child");
    REQUIRE(hParent != 0);
    REQUIRE(hChild  != 0);

    REQUIRE(AdsGotoRecord(hParent, 1) == 0);
    CHECK(set_id_and_write(hParent, "P999") == 0);

    // Child FK must be blank.
    REQUIRE(AdsGotoRecord(hChild, 1) == 0);
    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hChild, fld, buf, &cap, 0) == 0);
    bool all_blank = true;
    for (UNSIGNED32 i = 0; i < 4; ++i)
        if (buf[i] != ' ') { all_blank = false; break; }
    CHECK(all_blank);

    REQUIRE(AdsCloseTable(hChild)  == 0);
    REQUIRE(AdsCloseTable(hParent) == 0);
    REQUIRE(AdsDisconnect(hConn)   == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("RI: no DD → no enforcement (plain connection)") {
    // Open a plain directory (no .add) — writes should proceed without
    // any RI checks, even if the file has a field named "ID".
    auto dir = fs::temp_directory_path() / "openads_ri_no_dd";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_ri_dbf(dir, "data.dbf", {});

    UNSIGNED8 srv[512];
    auto s = dir.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hT = 0;
    UNSIGNED8 name[8] = "data";
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, 0, 0, 0, 0, &hT) == 0);

    REQUIRE(AdsAppendRecord(hT) == 0);
    UNSIGNED8 fld[4] = "ID";
    UNSIGNED8 val[8] = "ZZZZ";
    REQUIRE(AdsSetString(hT, fld, val, 4) == 0);
    CHECK(AdsWriteRecord(hT) == 0);   // no DD → no RI → succeeds

    REQUIRE(AdsCloseTable(hT)    == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
