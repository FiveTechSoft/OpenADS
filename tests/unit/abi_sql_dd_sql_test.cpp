// Tests for DD-related SQL:
//   EXECUTE PROCEDURE sp_*(...)  — 17 built-in stored procedures
//   CREATE DATABASE "path"
//   GRANT / REVOKE
//   system.columns / system.iota virtual tables

#include "doctest.h"

#include "openads/ace.h"
#include "openads/error.h"
#include "test_dd_make.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Minimal 0-record DBF with one C(10) field "NAME".
static void make_dbf(const std::string& path) {
    std::array<uint8_t, 97> buf{};
    buf[0] = 0x03;
    uint16_t hlen = 32 + 32 + 1;
    uint32_t rlen = 1 + 10;
    buf[8]  = static_cast<uint8_t>( hlen       & 0xFFu);
    buf[9]  = static_cast<uint8_t>((hlen >> 8) & 0xFFu);
    buf[10] = static_cast<uint8_t>( rlen       & 0xFFu);
    buf[11] = static_cast<uint8_t>((rlen >> 8) & 0xFFu);
    std::memcpy(&buf[32], "NAME      ", 10);
    buf[32 + 11] = 'C';
    buf[32 + 16] = 10;
    buf[64] = 0x0D;
    buf[65] = 0x1A;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
}

static void write_dd(const std::string& path, const std::string& body) {
    openads_test::make_dd(path, body);
}

// Execute SQL and return the record count from the resulting cursor.
static uint32_t sql_count(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0, cur = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return 0;
    std::vector<uint8_t> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return 0;
    }
    if (cur == 0) { AdsCloseSQLStatement(stmt); return 0; }
    uint32_t cnt = 0;
    AdsGetRecordCount(cur, ADS_IGNOREFILTERS, &cnt);
    AdsCloseTable(cur);
    AdsCloseSQLStatement(stmt);
    return cnt;
}

// Execute SQL with no cursor expected; return the error code.
static uint32_t sql_exec(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0, cur = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return 9999;
    std::vector<uint8_t> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    uint32_t rc = AdsExecuteSQLDirect(stmt, buf.data(), &cur);
    if (cur != 0) AdsCloseTable(cur);
    AdsCloseSQLStatement(stmt);
    return rc;
}

// Read the first row's named field as a trimmed string.
static std::string sql_field1(ADSHANDLE hConn, const char* sql, const char* field) {
    ADSHANDLE stmt = 0, cur = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return "";
    std::vector<uint8_t> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return "";
    }
    if (cur == 0) { AdsCloseSQLStatement(stmt); return ""; }
    AdsGotoTop(cur);
    std::array<uint8_t, 256> val{};
    uint32_t len = static_cast<uint32_t>(val.size() - 1);
    std::vector<uint8_t> fn(std::strlen(field) + 1);
    std::memcpy(fn.data(), field, fn.size());
    AdsGetField(cur, fn.data(), val.data(), &len, ADS_IGNOREFILTERS);
    AdsCloseTable(cur);
    AdsCloseSQLStatement(stmt);
    // Trim trailing spaces
    std::string s(reinterpret_cast<char*>(val.data()), len);
    auto p = s.find_last_not_of(' ');
    return (p == std::string::npos) ? "" : s.substr(0, p + 1);
}

} // namespace

// ---------------------------------------------------------------------------
// sp_CreateUser / sp_DropUser
// ---------------------------------------------------------------------------
TEST_CASE("sp_CreateUser and sp_DropUser via SQL") {
    auto dir = fs::temp_directory_path() / "oa_spuser_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_CreateUser('alice', 'secret', 'Test user')") == 0);
    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_CreateUser('bob', '', '')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.users") == 2);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_DropUser('bob')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.users") == 1);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// sp_CreateGroup / sp_DropGroup / sp_AddUserToGroup / sp_RemoveUserFromGroup
// ---------------------------------------------------------------------------
TEST_CASE("sp_Group management via SQL") {
    auto dir = fs::temp_directory_path() / "oa_spgrp_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "USER alice\n");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_CreateGroup('admins', 'Administrators')") == 0);
    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_AddUserToGroup('alice', 'admins')") == 0);

    // alice should now appear in system.usergroups
    CHECK(sql_count(hConn, "SELECT * FROM system.usergroups") >= 1);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_RemoveUserFromGroup('alice', 'admins')") == 0);
    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_DropGroup('admins')") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// sp_ModifyDatabase / sp_ModifyUserProperty
// ---------------------------------------------------------------------------
TEST_CASE("sp_ModifyDatabase and sp_ModifyUserProperty via SQL") {
    auto dir = fs::temp_directory_path() / "oa_spmod_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "USER alice\n");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_ModifyDatabase('COMMENT', 'My database')") == 0);
    // Verify property stored in system.dictionary
    CHECK(sql_count(hConn, "SELECT * FROM system.dictionary") >= 1);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_ModifyUserProperty('alice', 'PASSWORD', 'newpwd')") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// sp_AddTableToDatabase / sp_AddIndexFileToDatabase
// ---------------------------------------------------------------------------
TEST_CASE("sp_AddTableToDatabase and sp_AddIndexFileToDatabase via SQL") {
    auto dir = fs::temp_directory_path() / "oa_sptbl_test";
    fs::create_directories(dir);
    make_dbf((dir / "emp.dbf").string());
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_AddTableToDatabase('EMP', 'emp.dbf', 3, 1, '', 'Employee table')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.tables") == 1);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_AddIndexFileToDatabase('EMP', 'emp.cdx', 'main index')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.indexes") == 1);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// sp_CreateReferentialIntegrity / sp_DropReferentialIntegrity
// ---------------------------------------------------------------------------
TEST_CASE("sp_CreateReferentialIntegrity and sp_DropReferentialIntegrity via SQL") {
    auto dir = fs::temp_directory_path() / "oa_spri_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "TABLE PARENT=parent.dbf\nTABLE CHILD=child.dbf\n");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn,
        "EXECUTE PROCEDURE sp_CreateReferentialIntegrity('ri1', 'PARENT', 'CHILD', 'FK_ID', 2, 2, '', '', '')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.relations") == 1);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_DropReferentialIntegrity('ri1')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.relations") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// sp_CreateLink / sp_DropLink
// ---------------------------------------------------------------------------
TEST_CASE("sp_CreateLink and sp_DropLink via SQL") {
    auto dir = fs::temp_directory_path() / "oa_splink_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    REQUIRE(sql_exec(hConn,
        "EXECUTE PROCEDURE sp_CreateLink('lnk1', 'other.add', 0, 0, 0, '', '')") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.links") == 1);

    REQUIRE(sql_exec(hConn, "EXECUTE PROCEDURE sp_DropLink('lnk1', 0)") == 0);
    CHECK(sql_count(hConn, "SELECT * FROM system.links") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// CREATE DATABASE SQL
// ---------------------------------------------------------------------------
TEST_CASE("CREATE DATABASE SQL statement") {
    auto dir = fs::temp_directory_path() / "oa_cdb_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    // CREATE DATABASE with double-quoted relative path
    REQUIRE(sql_exec(hConn, "CREATE DATABASE \"newdb.add\"") == 0);
    CHECK(fs::exists(dir / "newdb.add"));

    // CREATE DATABASE with options
    REQUIRE(sql_exec(hConn, "CREATE DATABASE \"optdb.add\" DESCRIPTION 'Test DB'") == 0);
    CHECK(fs::exists(dir / "optdb.add"));

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// GRANT / REVOKE SQL
// ---------------------------------------------------------------------------
TEST_CASE("GRANT and REVOKE SQL statements") {
    auto dir = fs::temp_directory_path() / "oa_grant_test";
    fs::create_directories(dir);
    make_dbf((dir / "emp.dbf").string());
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "TABLE EMP=emp.dbf\nUSER alice\n");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    // No ACL yet → open access at runtime, but system.permissions still
    // lists EMP×alice with zero flags (legacy parity).
    CHECK(sql_count(hConn,
                    "SELECT * FROM system.permissions "
                    "WHERE OBJ_NAME = 'EMP' AND GRANTEE = 'alice'") == 1);

    REQUIRE(sql_exec(hConn, "GRANT SELECT ON EMP TO alice") == 0);
    // Now there should be an ACL entry
    CHECK(sql_count(hConn, "SELECT * FROM system.permissions") >= 1);

    REQUIRE(sql_exec(hConn, "GRANT ALL ON EMP TO alice") == 0);
    REQUIRE(sql_exec(hConn, "REVOKE ALL ON EMP FROM alice") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// system.iota virtual table
// ---------------------------------------------------------------------------
TEST_CASE("system.iota virtual table") {
    auto dir = fs::temp_directory_path() / "oa_iota_test";
    fs::create_directories(dir);
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    // system.iota has exactly one row
    CHECK(sql_count(hConn, "SELECT * FROM system.iota") == 1);

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// system.columns virtual table
// ---------------------------------------------------------------------------
TEST_CASE("system.columns virtual table") {
    auto dir = fs::temp_directory_path() / "oa_syscol_test";
    fs::create_directories(dir);
    make_dbf((dir / "emp.dbf").string());
    auto add_path = dir / "test.add";
    write_dd(add_path.string(), "TABLE EMP=emp.dbf\n");

    ADSHANDLE hConn = 0;
    std::vector<uint8_t> ap(add_path.string().size() + 1);
    std::memcpy(ap.data(), add_path.string().c_str(), ap.size());
    REQUIRE(AdsConnect60(ap.data(), ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hConn) == 0);

    // emp.dbf has 1 field (NAME C(10)) → system.columns should have 1 row
    uint32_t cnt = sql_count(hConn, "SELECT * FROM system.columns");
    CHECK(cnt == 1);

    // Verify the SAP schema: Parent (table) and Name (field) columns
    // (oracle-verified naming — S4).
    std::string tbl = sql_field1(hConn, "SELECT * FROM system.columns", "Parent");
    CHECK(tbl == "EMP");
    std::string col = sql_field1(hConn, "SELECT * FROM system.columns", "Name");
    CHECK(col == "NAME");

    AdsDisconnect(hConn);
    fs::remove_all(dir);
}
