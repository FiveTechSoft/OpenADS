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
    CHECK(read_col(hCur, "EXPR") == "4");

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
        CHECK(read_col(hCur, "EXPR") == "4");    // read WITHOUT AdsGotoTop
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
    auto sum = read_col(hCur, "EXPR");
    auto avg = read_col(hCur, "EXPR_1");
    auto mn  = read_col(hCur, "EXPR_2");
    auto mx  = read_col(hCur, "EXPR_3");
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
    auto cnt = read_col(hCur, "EXPR");
    auto sum = read_col(hCur, "EXPR_1");
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

// ---------------------------------------------------------------------------
// Aggregate results must carry SAP's declared PRESENTATION, not just the right
// number. Oracle-probed against SAP on the mp corpus (2026-07-30):
//
//     SUM            -> source width + 10, right-justified, source decimals
//     AVG/MIN/MAX    -> source width,      right-justified, source decimals
//     COUNT/COUNT(*) -> integral, NOT padded ("4", never "         4")
//
// All five aggregate materialisers used a hardcoded width of 20 and formatted
// left-justified, so every aggregate disagreed with SAP and with a plain read
// of the same column. agg_result_width() / agg_cell_text() own these rules now;
// if this test fails, one of the five paths has drifted from them.
// See docs/materialised-cursor-temps.md.
// ---------------------------------------------------------------------------
TEST_CASE("aggregate results use SAP's declared width and justification") {
    auto dir = fs::temp_directory_path() / "openads_agg_width";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto ds = dir.string();
    std::memcpy(srv, ds.c_str(), ds.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    // AsciiNumeric (ADT type 2) so the declared scale is carried on disk;
    // plain "Numeric" with decimals becomes an ADT DOUBLE, which stores none.
    UNSIGNED8 tname[32] = "money";
    UNSIGNED8 defs[96]  = "AMT,AsciiNumeric,10,2";
    ADSHANDLE hNew = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_ADT, 0, 0, 0, 0,
                           defs, &hNew) == 0);
    {
        UNSIGNED8 fld[8] = "AMT";
        for (const char* v : {"10.50", "20.25", "1.25"}) {
            REQUIRE(AdsAppendRecord(hNew) == 0);
            UNSIGNED8 b[16]{};
            std::memcpy(b, v, std::strlen(v));
            REQUIRE(AdsSetString(hNew, fld, b,
                        static_cast<UNSIGNED32>(std::strlen(v))) == 0);
            REQUIRE(AdsWriteRecord(hNew) == 0);
        }
    }
    REQUIRE(AdsCloseTable(hNew) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto raw = [&](const char* sql, const char* col) {
        UNSIGNED8 sb[160]{};
        std::memcpy(sb, sql, std::strlen(sql) + 1);
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sb, &hc) == 0);
        REQUIRE(AdsGotoTop(hc) == 0);
        UNSIGNED8 f[32]{};
        std::memcpy(f, col, std::strlen(col) + 1);
        UNSIGNED8 buf[128]{};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hc, f, buf, &cap, 0) == 0);
        std::string s(reinterpret_cast<const char*>(buf), cap);
        AdsCloseTable(hc);
        return s;
    };

    // AMT is N(10,2): SUM widens by 10 -> 20; AVG/MIN/MAX keep 10.
    const auto sum = raw("SELECT SUM(AMT) FROM money", "EXPR");
    CAPTURE(sum);
    CHECK(sum.size() == 20);
    CHECK(sum == "               32.00");

    const auto mn = raw("SELECT MIN(AMT) FROM money", "EXPR");
    CAPTURE(mn);
    CHECK(mn.size() == 10);
    CHECK(mn == "      1.25");

    const auto mx = raw("SELECT MAX(AMT) FROM money", "EXPR");
    CAPTURE(mx);
    CHECK(mx == "     20.25");

    // COUNT is integral and must NOT be padded — SAP returns "3".
    auto cnt = raw("SELECT COUNT(*) FROM money", "EXPR");
    while (!cnt.empty() && cnt.back() == ' ') cnt.pop_back();
    CAPTURE(cnt);
    CHECK(cnt == "3");
    CHECK(cnt.front() != ' ');

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ---------------------------------------------------------------------------
// MIN/MAX over a NON-NUMERIC column must compare the decoded text.
//
// The accumulators were all `double`, so a date or a name contributed
// as_double == 0 for every row and MIN(PAY_DATE) reported "0" — the aggregate
// silently answered a question about dates with a number. Dates decode to
// "YYYYMMDD", which orders lexicographically exactly as it does
// chronologically, so a plain string compare is correct for them.
//
// The result column is CHARACTER sized to the widest decoded value, not the
// source's on-disk width: an ADT date is 4 bytes on disk but 8 characters
// decoded, so sizing from the descriptor would truncate it back to "2009".
// ---------------------------------------------------------------------------
TEST_CASE("MIN/MAX over date and character columns compare as text") {
    auto dir = fs::temp_directory_path() / "openads_agg_minmax_text";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto ds = dir.string();
    std::memcpy(srv, ds.c_str(), ds.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    // ADT so the date column is a real 4-byte JDN, which is the case that
    // truncated; NAME exercises the character path.
    UNSIGNED8 tname[32] = "evts";
    UNSIGNED8 defs[96]  = "WHEN,Date;NAME,Character,20;GRP,Character,4";
    ADSHANDLE hNew = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_ADT, 0, 0, 0, 0,
                           defs, &hNew) == 0);
    {
        UNSIGNED8 fw[8] = "WHEN";
        UNSIGNED8 fn[8] = "NAME";
        UNSIGNED8 fg[8] = "GRP";
        struct Row { const char* d; const char* n; };
        const Row rows[] = {
            {"20110509", "MIKE"},
            {"20090816", "ALFA"},   // earliest date, and not the min name
            {"20170320", "ZULU"},   // latest date
            {"",         "BLNK"},   // blank date must not win MIN
        };
        for (const auto& r : rows) {
            REQUIRE(AdsAppendRecord(hNew) == 0);
            if (std::strlen(r.d) > 0) {
                UNSIGNED8 b[32]{};
                std::memcpy(b, r.d, std::strlen(r.d));
                REQUIRE(AdsSetString(hNew, fw, b,
                            static_cast<UNSIGNED32>(std::strlen(r.d))) == 0);
            }
            UNSIGNED8 b2[32]{};
            std::memcpy(b2, r.n, std::strlen(r.n));
            REQUIRE(AdsSetString(hNew, fn, b2,
                        static_cast<UNSIGNED32>(std::strlen(r.n))) == 0);
            UNSIGNED8 b3[8] = "G1";
            REQUIRE(AdsSetString(hNew, fg, b3, 2) == 0);
            REQUIRE(AdsWriteRecord(hNew) == 0);
        }
    }
    REQUIRE(AdsCloseTable(hNew) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto val = [&](const char* sql, const char* col) {
        UNSIGNED8 sb[160]{};
        std::memcpy(sb, sql, std::strlen(sql) + 1);
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sb, &hc) == 0);
        REQUIRE(AdsGotoTop(hc) == 0);
        UNSIGNED8 f[32]{};
        std::memcpy(f, col, std::strlen(col) + 1);
        UNSIGNED8 buf[128]{};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hc, f, buf, &cap, 0) == 0);
        std::string s(reinterpret_cast<const char*>(buf), cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        AdsCloseTable(hc);
        return s;
    };

    // Dates: full 8-character value, not "0" and not truncated to "2009".
    const auto lo = val("SELECT MIN(WHEN) FROM evts", "EXPR");
    const auto hi = val("SELECT MAX(WHEN) FROM evts", "EXPR");
    CAPTURE(lo); CAPTURE(hi);
    CHECK(lo == "20090816");
    CHECK(hi == "20170320");

    // Characters compare as text, and MIN is not simply the first row.
    CHECK(val("SELECT MIN(NAME) FROM evts", "EXPR") == "ALFA");
    CHECK(val("SELECT MAX(NAME) FROM evts", "EXPR") == "ZULU");

    // GROUP BY takes a different materialiser with its own accumulators, so
    // it is checked separately — all four aggregate paths had the same
    // double-only bug and were fixed together.
    CHECK(val("SELECT GRP, MIN(WHEN) FROM evts GROUP BY GRP ORDER BY GRP",
              "EXPR") == "20090816");

    // A blank value is absent, not a minimum. Row 4 has an empty date; without
    // the blank guard an empty string sorts below every real date and wins
    // every MIN, which is how the join paths first showed "" instead of a date.
    CHECK(val("SELECT MIN(WHEN) FROM evts", "EXPR") == "20090816");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// SUM(a * b) must evaluate the EXPRESSION in every aggregate materialiser.
// The scalar path got agg_arg_value() when arithmetic aggregates landed;
// the grouped and join paths kept reading only the bare left column, so
// SUM([real] * units) silently summed SUM(real). The mp gate never saw it
// because its bounded group's units were all 1 — it surfaced the day the
// unbounded 382K-group query first completed. Fixture rows use qty 2 and 3
// so the bare-column wrong answer (15.50 / 19.50) cannot collide with the
// right one.
TEST_CASE("SUM(a*b) evaluates the expression in grouped and join paths") {
    fs::path dir = fs::temp_directory_path() / "openads_agg_expr_paths";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    UNSIGNED8 srv[260] = {0};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    auto set_s = [](ADSHANDLE h, const char* f, const char* v) {
        UNSIGNED8 fb[32] = {0};
        std::strncpy(reinterpret_cast<char*>(fb), f, sizeof(fb) - 1);
        UNSIGNED8 vb[32] = {0};
        std::strncpy(reinterpret_cast<char*>(vb), v, sizeof(vb) - 1);
        REQUIRE(AdsSetString(h, fb, vb,
                             static_cast<UNSIGNED32>(std::strlen(v))) == 0);
    };
    {
        UNSIGNED8 t[] = "lines.adt";
        UNSIGNED8 d[] = "Grp,Character,2;Amt,AsciiNumeric,10,2;"
                        "Qty,AsciiNumeric,4";
        ADSHANDLE h = 0;
        REQUIRE(AdsCreateTable(hConn, t, nullptr, ADS_ADT, ADS_ANSI,
                               0, 0, 0, d, &h) == 0);
        const struct { const char* g; const char* a; const char* q; } rows[] =
            {{"A", "10.00", "2"}, {"A", "5.50", "3"}, {"B", "4.00", "1"}};
        for (const auto& r : rows) {
            REQUIRE(AdsAppendRecord(h) == 0);
            set_s(h, "Grp", r.g);
            set_s(h, "Amt", r.a);
            set_s(h, "Qty", r.q);
            REQUIRE(AdsWriteRecord(h) == 0);
        }
        REQUIRE(AdsCloseTable(h) == 0);
    }
    {
        UNSIGNED8 t[] = "grps.adt";
        UNSIGNED8 d[] = "Grp,Character,2;Label,Character,8";
        ADSHANDLE h = 0;
        REQUIRE(AdsCreateTable(hConn, t, nullptr, ADS_ADT, ADS_ANSI,
                               0, 0, 0, d, &h) == 0);
        for (const char* g : {"A", "B"}) {
            REQUIRE(AdsAppendRecord(h) == 0);
            set_s(h, "Grp", g);
            set_s(h, "Label", g);
            REQUIRE(AdsWriteRecord(h) == 0);
        }
        REQUIRE(AdsCloseTable(h) == 0);
    }

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    auto val = [&](const char* sql, const char* col) {
        UNSIGNED8 sb[256] = {0};
        std::memcpy(sb, sql, std::strlen(sql));
        ADSHANDLE hc = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sb, &hc) == 0);
        REQUIRE(AdsGotoTop(hc) == 0);
        std::string s = read_col(hc, col);
        AdsCloseTable(hc);
        auto b = s.find_first_not_of(' ');
        return b == std::string::npos ? std::string() : s.substr(b);
    };

    // Expression totals: 10*2 + 5.50*3 + 4*1 = 40.50; A = 36.50, B = 4.00.
    // The bare-column regression answers 19.50 / 15.50 / 4.00 instead.
    CHECK(val("SELECT SUM(Amt * Qty) AS T FROM lines", "T") == "40.50");
    CHECK(val("SELECT SUM(Amt * Qty) AS T, Grp FROM lines "
              "GROUP BY Grp", "T") == "36.50");                 // group A
    CHECK(val("SELECT SUM(Amt * Qty) AS T FROM lines l "
              "INNER JOIN grps g ON l.Grp = g.Grp", "T") == "40.50");
    CHECK(val("SELECT SUM(Amt * Qty) AS T FROM lines l "
              "INNER JOIN grps g ON l.Grp = g.Grp "
              "GROUP BY l.Grp", "T") == "36.50");               // group A

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
