#include "doctest.h"
#include "openads/ace.h"
#include "sql/parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
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
    hdr[4]  = 4;
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "AGE", 11);
    fd[11] = 'N'; fd[16] = 4; fd[17] = 0;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("  10"); rec("  20"); rec("  30"); rec("  40");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::string read_col(ADSHANDLE hCur, const char* name) {
    UNSIGNED8 fld[16] = {0};
    std::strcpy(reinterpret_cast<char*>(fld), name);
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    if (AdsGetField(hCur, fld, buf, &cap, 0) != 0) return {};
    auto s = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("M10.10 SELECT COUNT(*) returns matching row count") {
    auto dir = fs::temp_directory_path() / "openads_m10_10_count";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[160] = "SELECT COUNT(*) FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 1);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_col(hCur, "COL1") == "4");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// RCB 07/15/2026: after AdsExecuteSQLDirect the result cursor must be
// positioned ON the first row, exactly as SAP ADS does — NOT at BOF. OpenADS
// used to leave it at BOF, so a client that read the current record without a
// prior AdsGotoTop got a phantom empty row on every query (proven with a
// PMSYS parity dump: `SELECT COUNT(*)` returned two rows, blank then the
// count). The tests above all call AdsGotoTop first, which masked it. These
// deliberately do NOT — read immediately after execute, the SAP-supported
// pattern.
TEST_CASE("SQL result cursor is on row 1 after execute (no GotoTop) — SAP parity") {
    auto dir = fs::temp_directory_path() / "openads_sql_cursor_row1";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Aggregate: exactly one logical row. Reading it directly must yield the
    // count, and the cursor must not report BOF.
    {
        UNSIGNED8 sql[64] = "SELECT COUNT(*) FROM data.dbf";
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        UNSIGNED16 bof = 1;
        REQUIRE(AdsAtBOF(hCur, &bof) == 0);
        CHECK(bof == 0);                         // NOT before the first row
        CHECK(read_col(hCur, "COL1") == "4");    // read WITHOUT AdsGotoTop
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    // Plain SELECT: first record must be the first data row (AGE 10), read
    // directly. A BOF cursor would return a blank AGE here.
    {
        UNSIGNED8 sql[64] = "SELECT AGE FROM data.dbf";
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        // N(4,0) is right-justified ("  10"); strip leading blanks too. The
        // point is it's the first record's value, not an empty phantom row.
        std::string age = read_col(hCur, "AGE");
        age.erase(0, age.find_first_not_of(' '));
        CHECK(age == "10");                      // first row, no GotoTop
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.10 SUM / AVG / MIN / MAX") {
    auto dir = fs::temp_directory_path() / "openads_m10_10_agg";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT SUM(AGE), AVG(AGE), MIN(AGE), MAX(AGE) FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    REQUIRE(AdsGotoTop(hCur) == 0);
    auto sum = read_col(hCur, "COL1");
    auto avg = read_col(hCur, "COL2");
    auto mn  = read_col(hCur, "COL3");
    auto mx  = read_col(hCur, "COL4");
    CHECK(std::stod(sum) == doctest::Approx(100));
    CHECK(std::stod(avg) == doctest::Approx(25));
    CHECK(std::stod(mn)  == doctest::Approx(10));
    CHECK(std::stod(mx)  == doctest::Approx(40));

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.10 aggregate honours WHERE filter") {
    auto dir = fs::temp_directory_path() / "openads_m10_10_where";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // AGE values are stored as the ASCII strings "  10", "  20", "  30",
    // "  40". Comparing the raw column bytes against literal '20' uses
    // string semantics — only "  20" satisfies AGE = '  20' once the
    // engine fully pads the literal — but '20' (numeric) hits the
    // numeric path of the predicate via as_double. Use that.
    UNSIGNED8 sql[200] =
        "SELECT COUNT(*), SUM(AGE) FROM data.dbf WHERE AGE > 15";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    REQUIRE(AdsGotoTop(hCur) == 0);
    auto cnt = read_col(hCur, "COL1");
    auto sum = read_col(hCur, "COL2");
    CHECK(std::stoi(cnt) == 3);             // 20, 30, 40
    CHECK(std::stod(sum) == doctest::Approx(90));

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.10 mixing plain columns + aggregates rejected") {
    auto r = openads::sql::parse_select(
        "SELECT NAME, COUNT(*) FROM data");
    CHECK_FALSE(r.has_value());
}

namespace {
fs::path stage_grp_dbf(const fs::path& dir) {
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
    hdr[4]  = 5;                                     // 5 rows
    hdr[8]  = 32 + 32 * 2 + 1;                       // 2 columns
    hdr[10] = 1 + 4 + 4;                             // CITY C(4) + AMT N(4)
    push(hdr.data(), hdr.size());
    auto fld = [&](const char* nm, char ty, std::uint8_t L){
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), nm, 11);
        fd[11] = static_cast<std::uint8_t>(ty); fd[16] = L;
        push(fd.data(), fd.size());
    };
    fld("CITY", 'C', 4);
    fld("AMT",  'N', 4);
    file.push_back(0x0D);
    auto rec = [&](const char* city, const char* amt) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(city)
                           ? static_cast<std::uint8_t>(city[i]) : ' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(amt)
                           ? static_cast<std::uint8_t>(amt[i]) : ' ');
    };
    rec("NYC ", "  10");
    rec("NYC ", "  20");
    rec("LON ", "  30");
    rec("LON ", "  40");
    rec("PAR ", "   5");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}
}

TEST_CASE("M10.25 GROUP BY single column + COUNT/SUM") {
    auto dir = fs::temp_directory_path() / "openads_m10_25_grp";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_grp_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT COUNT(*), SUM(AMT) FROM data.dbf GROUP BY CITY";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);                                 // NYC / LON / PAR

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.30 GROUP BY + multi-comparison HAVING tree") {
    auto dir = fs::temp_directory_path() / "openads_m10_30_having";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_grp_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // 5-row CITY+AMT data:
    //   NYC,10  NYC,20  LON,30  LON,40  PAR,5
    // SUM by city: NYC=30, LON=70, PAR=5.
    // HAVING COUNT(*) > 1 AND SUM(AMT) > 50 → only LON.
    UNSIGNED8 sql[260] =
        "SELECT COUNT(*), SUM(AMT) FROM data.dbf "
        "GROUP BY CITY HAVING COUNT(*) > 1 AND SUM(AMT) > 50";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 1);                                 // LON only

    // OR-form: COUNT(*) > 1 OR SUM(AMT) > 100 → NYC + LON survive
    // (NYC has 2 rows; LON also has 2; PAR has 1 and SUM=5).
    UNSIGNED8 sql2[260] =
        "SELECT COUNT(*), SUM(AMT) FROM data.dbf "
        "GROUP BY CITY HAVING COUNT(*) > 1 OR SUM(AMT) > 100";
    ADSHANDLE hCur2 = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql2, &hCur2) == 0);
    UNSIGNED32 cnt2 = 0;
    REQUIRE(AdsGetRecordCount(hCur2, 0, &cnt2) == 0);
    CHECK(cnt2 == 2);                                // NYC + LON

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.25 GROUP BY + HAVING filters groups") {
    auto dir = fs::temp_directory_path() / "openads_m10_25_having";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_grp_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // PAR has 1 row, so HAVING COUNT(*) > 1 drops it.
    UNSIGNED8 sql[260] =
        "SELECT COUNT(*) FROM data.dbf GROUP BY CITY HAVING COUNT(*) > 1";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);                                 // NYC + LON

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
