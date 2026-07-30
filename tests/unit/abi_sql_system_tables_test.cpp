#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "test_dd_make.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Minimal DBF with one C(10) field "NAME" and zero records.
void make_dbf0(const fs::path& p) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    std::uint16_t hl = 32 + 32 + 1, rl = 1 + 10;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::memcpy(fd.data(), "NAME", 4);
    fd[11] = 'C'; fd[16] = 10;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

// DBF with one NAME C(10) record set to `val` (padded/truncated to 10 chars).
void make_dbf1(const fs::path& p, const char* val) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03; hdr[4] = 1;
    std::uint16_t hl = 32 + 32 + 1, rl = 1 + 10;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::memcpy(fd.data(), "NAME", 4);
    fd[11] = 'C'; fd[16] = 10;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' '); // not deleted
    std::array<std::uint8_t, 10> rec{};
    std::memset(rec.data(), ' ', 10);
    std::size_t n = std::min<std::size_t>(std::strlen(val), 10);
    std::memcpy(rec.data(), val, n);
    file.insert(file.end(), rec.begin(), rec.end());
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

// Create a DD file from a body of text-format lines.
void write_dd(const fs::path& p, const std::string& body) {
    openads_test::make_dd(p, body);
}

// Run a SELECT and return record count from the cursor.
int sql_count(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return -1;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return -1;
    }
    UNSIGNED32 cnt = 0;
    AdsGetRecordCount(cur, 0, &cnt);
    AdsCloseSQLStatement(stmt);
    return static_cast<int>(cnt);
}

// Run SELECT and collect the first column's trimmed values as strings.
std::vector<std::string> sql_col1(ADSHANDLE hConn, const char* sql,
                                   const char* field_name) {
    std::vector<std::string> out;
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return out;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return out;
    }
    UNSIGNED8 fname[64];
    std::memcpy(fname, field_name, std::strlen(field_name) + 1);
    AdsGotoTop(cur);
    for (int i = 0; i < 300; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(cur, &eof);
        if (eof) break;
        UNSIGNED8 vbuf[256] = {};
        UNSIGNED32 vlen = sizeof(vbuf) - 1;
        AdsGetField(cur, fname, vbuf, &vlen, 0);
        std::string s(reinterpret_cast<const char*>(vbuf), vlen);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        out.push_back(s);
        AdsSkip(cur, 1);
    }
    AdsCloseSQLStatement(stmt);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// system.tables
// ---------------------------------------------------------------------------

TEST_CASE("system.tables lists DD tables with correct TABLE_TYPE for DBF and ADT") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_tables";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);

    make_dbf0(dir / "emp.dbf");
    // ADT stub: content doesn't matter; type is inferred from extension.
    { std::ofstream f(dir / "dept.adt"); f.put(0); }

    write_dd(dir / "test.add",
             "TABLE emp=emp.dbf\n"
             "TABLE dept=dept.adt\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.tables") == 2);

    auto types = sql_col1(hConn, "SELECT * FROM system.tables", "Table_Type");
    bool has_dbf = false, has_adt = false;
    for (const auto& t : types) {
        if (t == "2") has_dbf = true;   // TABLE_TYPE 2 = CDX/DBF
        if (t == "3") has_adt = true;   // TABLE_TYPE 3 = ADT
    }
    CHECK(has_dbf);
    CHECK(has_adt);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.users + system.usergroups
// ---------------------------------------------------------------------------

TEST_CASE("system.users lists DD users") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_users";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "USER alice\n"
             "USER bob\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.users") == 2);

    // SAP's column name is Name (task #4 parity), not the old OpenADS USER_NAME.
    auto names = sql_col1(hConn, "SELECT * FROM system.users", "Name");
    bool has_alice = false, has_bob = false;
    for (const auto& n : names) {
        if (n == "alice") has_alice = true;
        if (n == "bob")   has_bob   = true;
    }
    CHECK(has_alice);
    CHECK(has_bob);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("system.usergroups lists memberships") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_ug";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "USER carol\n"
             "MEMBER carol=admins\n"
             "MEMBER carol=staff\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    // carol in 2 groups → 2 rows
    CHECK(sql_count(hConn, "SELECT * FROM system.usergroups") == 2);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.triggers
// ---------------------------------------------------------------------------

TEST_CASE("system.triggers lists DD triggers") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_trig";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "TRIGGER mytrig=t;3;1;1;mylib.dll;MyProc;\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.triggers") == 1);

    auto names = sql_col1(hConn, "SELECT * FROM system.triggers", "Name");
    REQUIRE(names.size() == 1u);
    CHECK(names[0] == "mytrig");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.storedprocedures
// ---------------------------------------------------------------------------

TEST_CASE("system.storedprocedures lists DD procs") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_proc";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "PROC myproc=mylib.dll;MyFunc;a,b;c;\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.storedprocedures") == 1);

    auto names = sql_col1(hConn, "SELECT * FROM system.storedprocedures", "Name");
    REQUIRE(names.size() == 1u);
    CHECK(names[0] == "myproc");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.functions
// ---------------------------------------------------------------------------

TEST_CASE("system.functions returns empty result set with Name column") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_func";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add", "TABLE t=t.dbf\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    // Must not return error 5018 (AE_NO_FILE_FOUND) — just an empty set.
    CHECK(sql_count(hConn, "SELECT * FROM system.functions") == 0);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.referentialintegrity (SAP alias for system.relations)
// ---------------------------------------------------------------------------

TEST_CASE("system.referentialintegrity lists RI rules and matches system.relations") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_ri";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "RI myrule=parent;child;pidx;cidx;1;2;fail\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.referentialintegrity") == 1);
    CHECK(sql_count(hConn, "SELECT * FROM system.relations") == 1);

    auto names = sql_col1(hConn, "SELECT * FROM system.referentialintegrity",
                          "RI_NAME");
    REQUIRE(names.size() == 1u);
    CHECK(names[0] == "myrule");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDGetRefIntegrityProperty returns RI fields") {
    const auto dir = fs::temp_directory_path() / "openads_ri_prop";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "RI myrule=Parent;Child;PTag;CTag;1;2;FailTbl\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    char buf[512]; UNSIGNED16 len;
    UNSIGNED8 rname[] = "myrule";
    len = sizeof(buf) - 1;
    REQUIRE(AdsDDGetRefIntegrityProperty(hConn, rname,
        ADS_DD_RI_PARENT, buf, &len) == 0);
    buf[len] = '\0';
    CHECK(std::string(buf) == "Parent");

    len = sizeof(buf) - 1;
    REQUIRE(AdsDDGetRefIntegrityProperty(hConn, rname,
        ADS_DD_RI_PARENT_TAG, buf, &len) == 0);
    buf[len] = '\0';
    CHECK(std::string(buf) == "PTag");

    len = sizeof(buf) - 1;
    REQUIRE(AdsDDGetRefIntegrityProperty(hConn, rname,
        ADS_DD_RI_CHILD_TAG, buf, &len) == 0);
    buf[len] = '\0';
    CHECK(std::string(buf) == "CTag");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsDDCreateProcedure stores comment via 8-param signature") {
    const auto dir = fs::temp_directory_path() / "openads_proc_comment";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add", "TABLE t=t.dbf\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 pname[]  = "myproc";
    UNSIGNED8 pcontr[] = "mylib.dll";
    UNSIGNED8 pfunc[]  = "MyFunc";
    UNSIGNED8 pin[]    = "@x INT";
    UNSIGNED8 pout[]   = "@y INT";
    UNSIGNED8 pcmt[]   = "does things";
    REQUIRE(AdsDDCreateProcedure(hConn, pname, pcontr, pfunc,
                                  0, pin, pout, pcmt) == 0);

    char buf[512]; UNSIGNED16 len;
    len = sizeof(buf) - 1;
    REQUIRE(AdsDDGetProcProperty(hConn, pname, ADS_DD_PROC_COMMENT,
                                  buf, &len) == 0);
    buf[len] = '\0';
    CHECK(std::string(buf) == "does things");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.views
// ---------------------------------------------------------------------------

TEST_CASE("system.views lists DD views") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_views";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "VIEW vw1=;SELECT * FROM t\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.views") == 1);

    auto names = sql_col1(hConn, "SELECT * FROM system.views", "Name");
    REQUIRE(names.size() == 1u);
    CHECK(names[0] == "vw1");

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.dictionary
// ---------------------------------------------------------------------------

TEST_CASE("system.dictionary lists DB properties") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_dict";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");
    write_dd(dir / "test.add",
             "TABLE t=t.dbf\n"
             "DBPROP version=2\n"
             "DBPROP description=TestDB\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM system.dictionary") == 2);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.* without a DD → error, not crash
// ---------------------------------------------------------------------------

TEST_CASE("system.tables without DD returns non-zero error code") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_nodd";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);
    make_dbf0(dir / "t.dbf");

    UNSIGNED8 srv[512];
    auto ds = dir.string();
    std::memcpy(srv, ds.c_str(), ds.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, 1, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE stmt = 0;
    AdsCreateSQLStatement(hConn, &stmt);
    UNSIGNED8 sql[] = "SELECT * FROM system.tables";
    ADSHANDLE cur = 0;
    UNSIGNED32 rc = AdsExecuteSQLDirect(stmt, sql, &cur);
    CHECK(rc != 0);

    AdsCloseSQLStatement(stmt);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// View alias expansion via AdsOpenTable
// ---------------------------------------------------------------------------

TEST_CASE("AdsOpenTable expands view alias to SQL cursor") {
    const auto dir = fs::temp_directory_path() / "openads_systbl_viewopen";
    std::error_code ec; fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // emp.dbf with one pre-written record.
    make_dbf1(dir / "emp.dbf", "Alice");

    write_dd(dir / "test.add",
             "TABLE emp=emp.dbf\n"
             "VIEW empview=;SELECT * FROM emp\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 vname[] = "empview";
    ADSHANDLE hView = 0;
    UNSIGNED32 rc = AdsOpenTable(hConn, vname, nullptr,
                                  ADS_CDX, ADS_ANSI,
                                  ADS_COMPATIBLE_LOCKING, 0,
                                  ADS_DEFAULT, &hView);
    REQUIRE(rc == 0);
    REQUIRE(hView != 0);

    UNSIGNED32 cnt = 0;
    AdsGetRecordCount(hView, 0, &cnt);
    CHECK(cnt == 1u);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// system.permissions — zero-rows for (grantee, object) without ACL
// ---------------------------------------------------------------------------

static UNSIGNED32 sql_exec(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return 1;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    const UNSIGNED32 rc = AdsExecuteSQLDirect(stmt, buf.data(), &cur);
    AdsCloseSQLStatement(stmt);
    return rc;
}

TEST_CASE("system.permissions emits zero-rows for ungranted grantee x object") {
    const auto dir = fs::temp_directory_path() / "openads_sysperm_zero";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    make_dbf0(dir / "a.dbf");
    make_dbf0(dir / "b.dbf");
    write_dd(dir / "test.add",
             "TABLE A=a.dbf\n"
             "TABLE B=b.dbf\n"
             "USER u1\n"
             "GROUP readers\n"
             "MEMBER u1=readers\n");

    UNSIGNED8 addpath[512];
    auto ap = (dir / "test.add").string();
    std::memcpy(addpath, ap.c_str(), ap.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(addpath, 1, nullptr, nullptr, 0, &hConn) == 0);

    CHECK(sql_count(hConn,
                    "SELECT * FROM system.permissions "
                    "WHERE GRANTEE = 'u1' AND Name = 'B'") == 1);

    REQUIRE(sql_exec(hConn, "GRANT SELECT ON A TO u1") == 0);
    CHECK(sql_count(hConn,
                    "SELECT * FROM system.permissions "
                    "WHERE GRANTEE = 'u1' AND Name = 'B'") == 1);
    CHECK(sql_count(hConn,
                    "SELECT * FROM system.permissions "
                    "WHERE GRANTEE = 'u1' AND Name = 'A'") == 1);
    CHECK(sql_count(hConn,
                    "SELECT * FROM system.permissions "
                    "WHERE GRANTEE = 'readers' AND Name = 'A'") == 1);

    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
