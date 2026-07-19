#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_dbf(const fs::path& path,
               const std::vector<std::pair<std::string,
                   std::pair<char, std::uint8_t>>>& schema,
               const std::vector<std::vector<std::string>>& rows) {
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(rows.size());
    std::uint16_t header_len = static_cast<std::uint16_t>(
        32 + 32 * schema.size() + 1);
    std::uint16_t rec_len = 1;
    for (auto& s : schema) rec_len += s.second.second;
    hdr[8]  = static_cast<std::uint8_t>( header_len       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((header_len >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rec_len          & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rec_len    >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    for (auto& s : schema) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()),
                     s.first.c_str(), 11);
        fd[11] = static_cast<std::uint8_t>(s.second.first);
        fd[16] = s.second.second;
        push(fd.data(), fd.size());
    }
    file.push_back(0x0D);
    for (auto& row : rows) {
        file.push_back(' ');
        for (std::size_t i = 0; i < schema.size(); ++i) {
            const auto& v = row[i];
            std::uint8_t L = schema[i].second.second;
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

}  // namespace

TEST_CASE("M10.14 INNER JOIN materialises matched rows") {
    auto dir = fs::temp_directory_path() / "openads_m10_14_join";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C002"},
         {"O03", "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf INNER JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 4);   // ID, CUST, R_CUST, R_NAME

    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8 rname[16] = "R_NAME";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, rname, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "Alice");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.34 JOIN + GROUP BY — COUNT/SUM per group") {
    auto dir = fs::temp_directory_path() / "openads_m10_34";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C001"},
         {"O03", "C002"},
         {"O04", "C002"},
         {"O05", "C002"},
         {"O06", "ZZZZ"}});                       // unmatched
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Group joined rows by R_NAME (right-side name).
    UNSIGNED8 sql[260] =
        "SELECT COUNT(*) FROM ord.dbf JOIN cus.dbf ON CUST = CUST "
        "GROUP BY R_NAME";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);                              // Alice (2) + Bob (3)

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.23 JOIN + aggregate — COUNT(*) over merged cursor") {
    auto dir = fs::temp_directory_path() / "openads_m10_23_jagg";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C001"},
         {"O03", "C002"},
         {"O04", "ZZZZ"}});                  // unmatched
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT COUNT(*) FROM ord.dbf JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 1);
    REQUIRE(AdsGotoRecord(hCur, 1) == 0);
    UNSIGNED8  buf[32];
    UNSIGNED32 sz = sizeof(buf);
    REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"EXPR", buf, &sz, 0) == 0);
    std::string s((char*)buf, sz);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "3");                          // O01,O02,O03 — O04 unmatched

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.22 FULL OUTER JOIN — union of LEFT + RIGHT") {
    auto dir = fs::temp_directory_path() / "openads_m10_22_full";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "ZZZZ"}});           // unmatched left
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C999", "Ghost"}});         // unmatched right

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf FULL OUTER JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);   // matched O01+Alice, unmatched O02 (left),
                       // unmatched Ghost (right)

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.21 RIGHT OUTER JOIN keeps right rows without a match") {
    auto dir = fs::temp_directory_path() / "openads_m10_21_right";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"}});                   // single order
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C999", "Ghost"}});                  // unmatched customer

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf RIGHT OUTER JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);   // every right (customer) row appears

    // Rec 2 is the unmatched Ghost row — left fields blank.
    REQUIRE(AdsGotoRecord(hCur, 2) == 0);
    UNSIGNED8 lid[16] = "ID";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, lid, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v.empty());   // blank left for unmatched right

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.16 LEFT OUTER JOIN keeps left rows without a match") {
    auto dir = fs::temp_directory_path() / "openads_m10_16_left";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "ZZZZ"}});   // no matching customer
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf LEFT OUTER JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);   // O01 matches Alice; O02 keeps left + blank right

    REQUIRE(AdsGotoRecord(hCur, 2) == 0);   // O02 row
    UNSIGNED8 rname[16] = "R_NAME";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, rname, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v.empty());   // blank right for outer-left match

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.20 JOIN combined with WHERE filters merged rows") {
    auto dir = fs::temp_directory_path() / "openads_m10_20_jw";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C002"},
         {"O03", "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[260] =
        "SELECT * FROM ord.dbf INNER JOIN cus.dbf ON CUST = CUST "
        "WHERE R_NAME = 'Alice'";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    std::set<UNSIGNED32> seq;
    REQUIRE(AdsGotoTop(hCur) == 0);
    while (true) {
        UNSIGNED16 atend = 0;
        if (AdsAtEOF(hCur, &atend) != 0 || atend) break;
        UNSIGNED32 r = 0;
        if (AdsGetRecordNum(hCur, 0, &r) != 0) break;
        seq.insert(r);
        if (AdsSkip(hCur, 1) != 0) break;
    }
    CHECK(seq.size() == 2);   // Alice rows = O01 + O03

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.20 JOIN combined with ORDER BY sorts merged rows") {
    auto dir = fs::temp_directory_path() / "openads_m10_20_jo";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"OB",  "C001"},
         {"OA",  "C002"},
         {"OC",  "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[260] =
        "SELECT * FROM ord.dbf INNER JOIN cus.dbf ON CUST = CUST "
        "ORDER BY ID";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    std::vector<std::string> ids;
    REQUIRE(AdsGotoTop(hCur) == 0);
    while (true) {
        UNSIGNED16 atend = 0;
        if (AdsAtEOF(hCur, &atend) != 0 || atend) break;
        UNSIGNED8 fld[16] = "ID";
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hCur, fld, buf, &cap, 0) == 0);
        std::string v(reinterpret_cast<const char*>(buf), cap);
        while (!v.empty() && v.back() == ' ') v.pop_back();
        ids.push_back(v);
        if (AdsSkip(hCur, 1) != 0) break;
    }
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "OA");
    CHECK(ids[1] == "OB");
    CHECK(ids[2] == "OC");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.14 INNER JOIN drops rows without a match") {
    auto dir = fs::temp_directory_path() / "openads_m10_14_join_miss";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "ZZZZ"}});   // no matching customer
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf INNER JOIN cus.dbf ON CUST = CUST";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 1);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// --- ADS dialect comma-join (feat/sql-comma-join, #6) ----------------------
// `FROM a, b WHERE a.x = b.y` is the SQL-89 comma-join legacy ADS apps write
// instead of `INNER JOIN ... ON`. The parser lowers it onto the same single
// inner_join AST, so it must produce byte-identical results to the explicit
// INNER JOIN form.

TEST_CASE("comma-join: FROM a, b WHERE a.x = b.y == explicit INNER JOIN") {
    auto dir = fs::temp_directory_path() / "openads_comma_join_basic";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C002"},
         {"O03", "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf, cus.dbf WHERE ord.cust = cus.cust";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);

    UNSIGNED16 nf = 0;
    REQUIRE(AdsGetNumFields(hCur, &nf) == 0);
    CHECK(nf == 4);   // ID, CUST, R_CUST, R_NAME

    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8 rname[16] = "R_NAME";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, rname, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "Alice");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("comma-join: join key written joined-table-first still resolves") {
    // The base table is `ord` but the predicate names the joined table
    // first (`cus.code = ord.cust`) and the join columns have different
    // names. Alias stripping loses the table binding, so the executor must
    // resolve the orientation against the real schemas rather than assume
    // the left predicate column belongs to the base table.
    auto dir = fs::temp_directory_path() / "openads_comma_join_revorient";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C002"},
         {"O03", "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CODE", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[200] =
        "SELECT * FROM ord.dbf, cus.dbf WHERE cus.code = ord.cust";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 3);

    REQUIRE(AdsGotoTop(hCur) == 0);
    UNSIGNED8 rname[16] = "R_NAME";
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hCur, rname, buf, &cap, 0) == 0);
    auto v = std::string(reinterpret_cast<const char*>(buf), cap);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "Alice");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("comma-join: residual WHERE filter applies after the join key") {
    auto dir = fs::temp_directory_path() / "openads_comma_join_filter";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "ord.dbf",
        {{"ID",  {'C', 4}}, {"CUST", {'C', 4}}},
        {{"O01", "C001"},
         {"O02", "C002"},
         {"O03", "C001"}});
    write_dbf(dir / "cus.dbf",
        {{"CUST", {'C', 4}}, {"NAME", {'C', 8}}},
        {{"C001", "Alice"},
         {"C002", "Bob"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Residual filter on a base-table column survives the join-key lift
    // (keeps only ord row O02, which joins to C002/Bob).
    UNSIGNED8 sql[220] =
        "SELECT * FROM ord.dbf, cus.dbf "
        "WHERE ord.cust = cus.cust AND ord.id = 'O02'";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 1);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
