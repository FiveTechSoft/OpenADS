#include "doctest.h"
#include "openads/ace.h"
#include "sql/parser.h"
#include "drivers/cdx/cdx_index.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_two_field_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p);
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 0;
    hdr[8]  = 32 + 32 + 32 + 1;
    hdr[10] = 1 + 6 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C'; name_fd[16] = 6;
    push(name_fd.data(), name_fd.size());
    std::array<std::uint8_t, 32> age_fd{};
    std::strncpy(reinterpret_cast<char*>(age_fd.data()), "AGE", 11);
    age_fd[11] = 'N'; age_fd[16] = 4; age_fd[17] = 0;
    push(age_fd.data(), age_fd.size());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M10.5 parse_insert recognises INSERT INTO ... VALUES ...") {
    auto r = openads::sql::parse_insert(
        "INSERT INTO data (NAME, AGE) VALUES ('Alice', 30)");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data");
    REQUIRE(r.value().columns.size() == 2);
    CHECK(r.value().columns[0] == "NAME");
    CHECK(r.value().columns[1] == "AGE");
    REQUIRE(r.value().values.size() == 2);
    CHECK_FALSE(r.value().values[0].is_numeric);
    CHECK(r.value().values[0].text == "Alice");
    CHECK(r.value().values[1].is_numeric);
    CHECK(r.value().values[1].number == doctest::Approx(30));
}

TEST_CASE("M10.5 parse_insert rejects column / value count mismatch") {
    auto r = openads::sql::parse_insert(
        "INSERT INTO data (NAME, AGE) VALUES ('Alice')");
    CHECK_FALSE(r.has_value());
}

TEST_CASE("M10.5 sql_is_insert dispatches by leading keyword") {
    CHECK(openads::sql::sql_is_insert("INSERT INTO x ..."));
    CHECK(openads::sql::sql_is_insert("  insert into x ..."));
    CHECK_FALSE(openads::sql::sql_is_insert("SELECT * FROM x"));
}

TEST_CASE("M10.5 INSERT through AdsExecuteSQLDirect appends a row") {
    auto dir = fs::temp_directory_path() / "openads_m10_5_insert";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_two_field_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql1[160] = "INSERT INTO data (NAME, AGE) VALUES ('Alice', 30)";
    ADSHANDLE hCur = 0xDEADBEEF;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql1, &hCur) == 0);
    CHECK(hCur == 0);   // INSERT returns no cursor

    UNSIGNED8 sql2[160] = "INSERT INTO data (NAME, AGE) VALUES ('Bob', 42)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql2, &hCur) == 0);

    // Reopen + verify rows landed.
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8 fld[16] = "NAME";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap)
          .substr(0, 5) == "Alice");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap)
          .substr(0, 3) == "Bob");

    // AGE is a numeric column; AdsGetField returns its ASCII repr.
    UNSIGNED8 age_fld[16] = "AGE";
    UNSIGNED8 abuf[16] = {0};
    UNSIGNED32 acap = sizeof(abuf);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetField(hTable, age_fld, abuf, &acap, 0) == 0);
    auto age_str = std::string(reinterpret_cast<const char*>(abuf), acap);
    while (!age_str.empty() && age_str.front() == ' ') age_str.erase(age_str.begin());
    while (!age_str.empty() && age_str.back()  == ' ') age_str.pop_back();
    CHECK(age_str == "30");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Regression: SQL DML opened the table through the ENGINE connection, which
// never bound the structural bag — every INSERT/UPDATE/DELETE left a
// production CDX stale (0 keys), and the next opener navigated a ghost order
// (bof=eof=1 Limbo over a non-empty table; Pritpal Bedi's .z01 work bags,
// Aug 2026). DML must now bind <base>.cdx for the life of the statement so
// commit_dirty_record keeps it current.
//
// The check below opens the bag at DRIVER level on purpose: the ABI open
// path now also HEALS stale bags on AdsOpenIndex, which would mask a DML
// that never wrote its keys.
TEST_CASE("SQL INSERT/DELETE maintain the structural CDX bag") {
    auto dir = fs::temp_directory_path() / "openads_dml_prod_index";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "dmlidx";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    // Structural bag (dmlidx.cdx == <base>.cdx) built while empty.
    auto idx_path = (dir / "dmlidx.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64]  = "BYID";
    UNSIGNED8 expr[64] = "ID";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0,
                           &hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);   // closes + flushes the bag

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    ADSHANDLE hCur = 0;
    UNSIGNED8 sql1[160] = "INSERT INTO dmlidx (ID) VALUES (10)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql1, &hCur) == 0);
    UNSIGNED8 sql2[160] = "INSERT INTO dmlidx (ID) VALUES (20)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql2, &hCur) == 0);
    UNSIGNED8 sql3[160] = "INSERT INTO dmlidx (ID) VALUES (30)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql3, &hCur) == 0);
    UNSIGNED8 sql4[160] = "DELETE FROM dmlidx WHERE ID = 20";
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql4, &hCur) == 0);
    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);

    // Driver-level: the bag must hold a key per surviving row. DBFCDX keeps
    // DELETED rows in the index (SET DELETED hides them at navigation), so
    // the DELETE above leaves its key in place: 3 keys, one flagged deleted.
    openads::drivers::cdx::CdxIndex idx;
    REQUIRE(idx.open_named(idx_path,
                           openads::drivers::IndexOpenMode::Shared, "BYID"));
    CHECK_FALSE(idx.empty());              // RED before the fix (0 keys)
    int keys = 0;
    auto s = idx.seek_first();
    while (s && s.value().positioned && keys < 50) {
        ++keys;
        s = idx.next();
    }
    CHECK(keys == 3);
    (void)idx.flush();

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
