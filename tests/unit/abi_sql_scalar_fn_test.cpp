#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_dbf_typed(const fs::path& path,
                     const std::vector<std::tuple<std::string, char,
                                                   std::uint8_t>>& cols,
                     const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(rows.size());
    std::uint16_t hl = static_cast<std::uint16_t>(32 + 32 * cols.size() + 1);
    std::uint16_t rl = 1;
    for (auto& c : cols) rl += std::get<2>(c);
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    for (auto& c : cols) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()),
                     std::get<0>(c).c_str(), 11);
        fd[11] = static_cast<std::uint8_t>(std::get<1>(c));
        fd[16] = std::get<2>(c);
        push(fd.data(), fd.size());
    }
    file.push_back(0x0D);
    for (auto& row : rows) {
        file.push_back(' ');
        for (std::size_t i = 0; i < cols.size(); ++i) {
            const auto& v = row[i];
            std::uint8_t L = std::get<2>(cols[i]);
            for (std::uint8_t k = 0; k < L; ++k) {
                file.push_back(k < v.size()
                    ? static_cast<std::uint8_t>(v[k]) : ' ');
            }
        }
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

std::string read_field(ADSHANDLE hCur, const char* name) {
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)name, buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("M10.39 UPPER / LOWER / LEN / TRIM in projection") {
    auto dir = fs::temp_directory_path() / "openads_m10_39";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"NAME", 'C', 8}},
        {{"  bob  "},
         {"alice"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "SELECT UPPER(NAME) AS U, LOWER(NAME) AS L, "
        "TRIM(NAME) AS T, LEN(NAME) AS N FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "U") == "  BOB");
    CHECK(read_field(hCur, "L") == "  bob");
    CHECK(read_field(hCur, "T") == "bob");
    CHECK(read_field(hCur, "N") == "5");                  // "  bob" len-trim-right

    REQUIRE(AdsSkip(hCur, 1) == 0);
    CHECK(read_field(hCur, "U") == "ALICE");
    CHECK(read_field(hCur, "T") == "alice");
    CHECK(read_field(hCur, "N") == "5");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.43 SUBSTR / CONCAT / REPLACE multi-arg fns") {
    auto dir = fs::temp_directory_path() / "openads_m10_43";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"NAME", 'C', 8}, {"SUFFIX", 'C', 4}},
        {{"AliceX", " Inc"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "SELECT SUBSTR(NAME, 1, 5) AS S, "
        "CONCAT(NAME, SUFFIX) AS C, "
        "REPLACE(NAME, 'Alice', 'Bob') AS R FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "S") == "Alice");
    CHECK(read_field(hCur, "C") == "AliceX Inc");
    CHECK(read_field(hCur, "R") == "BobX");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.53 NULLIF / COALESCE / IFNULL") {
    auto dir = fs::temp_directory_path() / "openads_m10_53";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"A", 'C', 5}, {"B", 'C', 5}},
        {{"hi", "hi"},
         {"", "fb"},
         {"foo", "bar"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "SELECT NULLIF(A, B) AS X, COALESCE(A, B) AS Y, "
        "IFNULL(A, 'zz') AS Z FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "X") == "");          // NULLIF: equal → null
    CHECK(read_field(hCur, "Y") == "hi");        // COALESCE: A non-empty
    CHECK(read_field(hCur, "Z") == "hi");

    REQUIRE(AdsSkip(hCur, 1) == 0);
    CHECK(read_field(hCur, "X") == "");          // NULLIF on empty A,'fb' = '' (because A is "")
    CHECK(read_field(hCur, "Y") == "fb");        // COALESCE: A empty → B
    CHECK(read_field(hCur, "Z") == "zz");        // IFNULL: A empty → 'zz'

    REQUIRE(AdsSkip(hCur, 1) == 0);
    CHECK(read_field(hCur, "X") == "foo");       // NULLIF: not equal → A
    CHECK(read_field(hCur, "Y") == "foo");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.54 aggregate FILTER (WHERE ...)") {
    auto dir = fs::temp_directory_path() / "openads_m10_54";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"AGE", 'N', 4}},
        {{"  10"}, {"  25"}, {"  40"}, {"  55"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "SELECT COUNT(*) FILTER (WHERE AGE > 20), "
        "COUNT(*) FILTER (WHERE AGE > 50) FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "EXPR") == "3");      // 25 / 40 / 55
    CHECK(read_field(hCur, "EXPR_1") == "1");    // only 55

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.45 DATEDIFF / DATEADD on YYYYMMDD strings") {
    auto dir = fs::temp_directory_path() / "openads_m10_45";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"START_", 'C', 8}, {"END_", 'C', 8}},
        {{"20260101", "20260131"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[300] =
        "SELECT DATEDIFF(END_, START_) AS DAYS, "
        "DATEADD(START_, 7) AS NEXT_W FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "DAYS") == "30");
    CHECK(read_field(hCur, "NEXT_W") == "20260108");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.40 arithmetic in projection") {
    auto dir = fs::temp_directory_path() / "openads_m10_40";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"A", 'N', 4}, {"B", 'N', 4}},
        {{"  10", "   3"},
         {"  20", "   5"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[260] =
        "SELECT A + B AS S, A * 2 AS D, A - B AS X FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    CHECK(read_field(hCur, "S") == "13");
    CHECK(read_field(hCur, "D") == "20");
    CHECK(read_field(hCur, "X") == "7");

    REQUIRE(AdsSkip(hCur, 1) == 0);
    CHECK(read_field(hCur, "S") == "25");
    CHECK(read_field(hCur, "D") == "40");
    CHECK(read_field(hCur, "X") == "15");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ADS dialect: UPPER/LOWER on the WHERE left-hand side") {
    auto dir = fs::temp_directory_path() / "openads_ads_where_fn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // REF mixes case so UPPER/LOWER folding is observable; NAME drives LIKE.
    write_dbf_typed(dir / "data.dbf",
        {{"REF", 'C', 4}, {"NAME", 'C', 8}},
        {{"N", "alice"},
         {"n", "Bob"},
         {"S", "amy"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto run_count = [&](const char* q) -> UNSIGNED32 {
        UNSIGNED8 sql[256];
        std::memcpy(sql, q, std::strlen(q) + 1);
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        UNSIGNED32 count = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &count) == 0);
        return count;
    };

    // UPPER folds 'N' and 'n' together; <> 'N' leaves only the 'S' row.
    CHECK(run_count("SELECT * FROM data.dbf WHERE UPPER(REF) <> 'N'") == 1);
    CHECK(run_count("SELECT * FROM data.dbf WHERE UPPER(REF) = 'N'")  == 2);
    // LOWER mirror: = 'n' also matches both the 'N' and 'n' rows.
    CHECK(run_count("SELECT * FROM data.dbf WHERE LOWER(REF) = 'n'")  == 2);
    // UPPER on the LIKE path: ALICE and AMY match 'A%', BOB does not.
    CHECK(run_count("SELECT * FROM data.dbf WHERE UPPER(NAME) LIKE 'A%'") == 2);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ADS dialect: WHERE 1=1 constant predicate + full search query") {
    auto dir = fs::temp_directory_path() / "openads_ads_const_pred";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"REF", 'C', 4}, {"NAME", 'C', 8}},
        {{"N", "alice"},
         {"n", "Bob"},
         {"S", "amy"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    auto run_count = [&](const char* q) -> UNSIGNED32 {
        UNSIGNED8 sql[300];
        std::memcpy(sql, q, std::strlen(q) + 1);
        ADSHANDLE c = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &c) == 0);
        UNSIGNED32 n = 0;
        REQUIRE(AdsGetRecordCount(c, 0, &n) == 0);
        return n;
    };

    // 1=1 folds to always-true (all rows); 1=2 to always-false (none).
    CHECK(run_count("SELECT * FROM data.dbf WHERE 1 = 1") == 3);
    CHECK(run_count("SELECT * FROM data.dbf WHERE 1 = 2") == 0);
    // Mixed with a real predicate, the boilerplate is transparent.
    CHECK(run_count("SELECT * FROM data.dbf WHERE 1 = 1 AND REF = 'S'") == 1);
    // The full legacy-ERP search shape (hint + bracket + alias + 1=1 +
    // UPPER folds + LIKE + ORDER BY) executes and filters correctly.
    CHECK(run_count(
        "SELECT {static} * FROM [data.dbf] AS a "
        "WHERE 1 = 1 "
        "AND UPPER(a.REF) <> 'N' "
        "AND UPPER(a.NAME) LIKE 'A%' "
        "ORDER BY a.NAME") == 1);   // only the 'S'/'amy' row

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("ADS dialect: UPPER() in WHERE drives UPDATE / DELETE row scope") {
    auto dir = fs::temp_directory_path() / "openads_ads_where_fn_dml";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf_typed(dir / "data.dbf",
        {{"REF", 'C', 4}, {"TAG", 'C', 4}},
        {{"N", "a"},
         {"n", "b"},
         {"S", "c"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // UPDATE folds REF: both the 'N' and 'n' rows get TAG='X'; 'S' untouched.
    UNSIGNED8 upd[200] =
        "UPDATE data.dbf SET TAG = 'X' WHERE UPPER(REF) = 'N'";
    ADSHANDLE hCur = 0xDEADBEEF;
    REQUIRE(AdsExecuteSQLDirect(hStmt, upd, &hCur) == 0);

    auto run_count = [&](const char* q) -> UNSIGNED32 {
        UNSIGNED8 sql[200];
        std::memcpy(sql, q, std::strlen(q) + 1);
        ADSHANDLE c = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &c) == 0);
        UNSIGNED32 n = 0;
        REQUIRE(AdsGetRecordCount(c, 0, &n) == 0);
        return n;
    };
    CHECK(run_count("SELECT * FROM data.dbf WHERE TAG = 'X'") == 2);

    // DELETE folds REF too: removing UPPER(REF)='N' tombstones the 'N' and
    // 'n' rows (records 1 and 2) and leaves the 'S' row (record 3).
    UNSIGNED8 del[200] =
        "DELETE FROM data.dbf WHERE UPPER(REF) = 'N'";
    hCur = 0xDEADBEEF;
    REQUIRE(AdsExecuteSQLDirect(hStmt, del, &hCur) == 0);

    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED16 deleted = 0;
    REQUIRE(AdsGotoRecord(hTable, 1) == 0);
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted != 0);                       // 'N' row gone
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted != 0);                       // 'n' row gone (UPPER folded)
    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    REQUIRE(AdsIsRecordDeleted(hTable, &deleted) == 0);
    CHECK(deleted == 0);                       // 'S' row survives
    REQUIRE(AdsCloseTable(hTable) == 0);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
