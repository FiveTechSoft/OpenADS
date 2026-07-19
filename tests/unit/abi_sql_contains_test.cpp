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
            for (std::uint8_t k = 0; k < L; ++k)
                file.push_back(k < v.size()
                    ? static_cast<std::uint8_t>(v[k]) : ' ');
        }
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

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
    hdr[10] = 1 + 50;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NOTES", 11);
    fd[11] = 'C'; fd[16] = 50;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 50; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("Quick brown fox jumps over lazy dog");
    rec("Lazy dog sleeps in sun");
    rec("Quick fox returns to den at dusk");
    rec("Brown bear eats honey from hive");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

void make_fts(ADSHANDLE hConn) {
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED8 file[64]   = "data.fts";
    UNSIGNED8 tag[16]    = "T";
    UNSIGNED8 field[16]  = "NOTES";
    UNSIGNED8 nullbuf[2] = {0, 0};
    REQUIRE(AdsCreateFTSIndex(hTable, file, tag, field,
                              0, 3, 30,
                              1, nullbuf, 1, nullbuf, 1, nullbuf,
                              1, nullbuf, nullbuf, nullbuf, 0) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
}

}  // namespace

TEST_CASE("M9.21 SQL CONTAINS lowers through FTS hit set") {
    auto dir = fs::temp_directory_path() / "openads_m9_21_sql_contains";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    make_fts(hConn);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // SELECT * FROM data WHERE CONTAINS(NOTES, 'fox') — recnos 1 and 3.
    UNSIGNED8 sql[128] = "SELECT * FROM data WHERE CONTAINS(NOTES, 'fox')";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    // Walk the cursor and collect recnos.
    REQUIRE(AdsGotoTop(hCur) == 0);
    std::set<UNSIGNED32> got;
    while (true) {
        UNSIGNED16 atend = 0;
        REQUIRE(AdsAtEOF(hCur, &atend) == 0);
        if (atend) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hCur, 0, &r) == 0);
        got.insert(r);
        REQUIRE(AdsSkip(hCur, 1) == 0);
    }
    CHECK(got == std::set<UNSIGNED32>{1, 3});

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.21 SQL CONTAINS combines with regular AND clause") {
    auto dir = fs::temp_directory_path() / "openads_m9_21_sql_contains_and";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    make_fts(hConn);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Both predicates must match. CONTAINS('lazy dog') hits {1,2}; the
    // second clause keeps only NOTES values that sort before 'M', so
    // "Lazy dog…" stays (rec 2) and "Quick brown…" drops (rec 1).
    UNSIGNED8 sql[160] =
        "SELECT * FROM data WHERE CONTAINS(NOTES, 'lazy dog')"
        " AND NOTES <= 'M'";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    REQUIRE(AdsGotoTop(hCur) == 0);
    std::set<UNSIGNED32> got;
    while (true) {
        UNSIGNED16 atend = 0;
        REQUIRE(AdsAtEOF(hCur, &atend) == 0);
        if (atend) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hCur, 0, &r) == 0);
        got.insert(r);
        REQUIRE(AdsSkip(hCur, 1) == 0);
    }
    CHECK(got == std::set<UNSIGNED32>{2});

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.21 SQL CONTAINS without prebuilt .fts errors out") {
    auto dir = fs::temp_directory_path() / "openads_m9_21_sql_contains_missing";
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
    UNSIGNED8 sql[128] = "SELECT * FROM data WHERE CONTAINS(NOTES, 'fox')";
    ADSHANDLE hCur = 0;
    UNSIGNED32 rc = AdsExecuteSQLDirect(hStmt, sql, &hCur);
    CHECK(rc != 0);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.like SQL LIKE in join cursor WHERE") {
    auto dir = fs::temp_directory_path() / "openads_m10_like_join";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "empl.dbf",
        {{"ID",   {'C', 4}}, {"NAME", {'C', 10}}},
        {{"E001", "Alice"},
         {"E002", "Bob"},
         {"E003", "Alan"}});
    write_dbf(dir / "dept.dbf",
        {{"ID",   {'C', 4}}, {"DEPT", {'C', 8}}},
        {{"E001", "Eng"},
         {"E002", "Mktg"},
         {"E003", "Eng"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Join, then filter on the left-side NAME using LIKE.
    UNSIGNED8 sql[256] =
        "SELECT * FROM empl.dbf INNER JOIN dept.dbf ON ID = ID "
        "WHERE NAME LIKE 'Al%'";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
    CHECK(cnt == 2);  // Alice (E001) and Alan (E003) both match

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.like SQL LIKE in aggregate FILTER") {
    auto dir = fs::temp_directory_path() / "openads_m10_like_agg_filter";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    write_dbf(dir / "data.dbf",
        {{"NAME", {'C', 10}}},
        {{"Alice"},
         {"Bob"},
         {"Alan"},
         {"Carol"}});

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    UNSIGNED8 sql[256] =
        "SELECT COUNT(*) FILTER (WHERE NAME LIKE 'Al%'), "
        "COUNT(*) FILTER (WHERE NAME LIKE '%o%') FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);

    UNSIGNED8 buf1[32] = {0}; UNSIGNED32 cap1 = sizeof(buf1);
    UNSIGNED8 col1[8] = "EXPR";
    REQUIRE(AdsGetField(hCur, col1, buf1, &cap1, 0) == 0);
    auto s1 = std::string(reinterpret_cast<const char*>(buf1), cap1);
    while (!s1.empty() && s1.back() == ' ') s1.pop_back();
    CHECK(s1 == "2");  // Alice, Alan

    UNSIGNED8 buf2[32] = {0}; UNSIGNED32 cap2 = sizeof(buf2);
    UNSIGNED8 col2[8] = "EXPR_1";
    REQUIRE(AdsGetField(hCur, col2, buf2, &cap2, 0) == 0);
    auto s2 = std::string(reinterpret_cast<const char*>(buf2), cap2);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "2");  // Bob, Carol

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.contains SQL CONTAINS in aggregate FILTER") {
    auto dir = fs::temp_directory_path() / "openads_m10_contains_agg_filter";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    make_fts(hConn);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Records 1 and 3 contain "fox"; total row count is 4.
    UNSIGNED8 sql[256] =
        "SELECT COUNT(*), COUNT(*) FILTER (WHERE CONTAINS(NOTES, 'fox')) "
        "FROM data.dbf";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);

    UNSIGNED8 buf1[32] = {0}; UNSIGNED32 cap1 = sizeof(buf1);
    UNSIGNED8 col1[8] = "EXPR";
    REQUIRE(AdsGetField(hCur, col1, buf1, &cap1, 0) == 0);
    auto s1 = std::string(reinterpret_cast<const char*>(buf1), cap1);
    while (!s1.empty() && s1.back() == ' ') s1.pop_back();
    CHECK(s1 == "4");  // all rows

    UNSIGNED8 buf2[32] = {0}; UNSIGNED32 cap2 = sizeof(buf2);
    UNSIGNED8 col2[8] = "EXPR_1";
    REQUIRE(AdsGetField(hCur, col2, buf2, &cap2, 0) == 0);
    auto s2 = std::string(reinterpret_cast<const char*>(buf2), cap2);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "2");  // records 1 and 3

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
