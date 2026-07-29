#include "doctest.h"
#include "openads/ace.h"

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
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    rec("CCCC"); rec("AAAA"); rec("DDDD"); rec("BBBB");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Walk the cursor and collect TAG field values (the ordered data). After
// #136 ORDER BY materialises a static cursor, recnos are positional 1..N
// rather than source recnos — so tests assert sorted *data*, not source
// record numbers.
std::vector<std::string> walk_tags(ADSHANDLE hCur) {
    std::vector<std::string> out;
    if (AdsGotoTop(hCur) != 0) return out;
    UNSIGNED8 fld[8] = "TAG";
    while (true) {
        UNSIGNED16 atend = 0;
        if (AdsAtEOF(hCur, &atend) != 0 || atend) break;
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        if (AdsGetField(hCur, fld, buf, &cap, 0) != 0) break;
        std::string s(reinterpret_cast<char*>(buf), cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        out.push_back(std::move(s));
        if (AdsSkip(hCur, 1) != 0) break;
    }
    return out;
}

// Positional recnos on a materialised cursor must be 1..N in walk order.
std::vector<UNSIGNED32> walk_recnos(ADSHANDLE hCur) {
    std::vector<UNSIGNED32> out;
    if (AdsGotoTop(hCur) != 0) return out;
    while (true) {
        UNSIGNED16 atend = 0;
        if (AdsAtEOF(hCur, &atend) != 0 || atend) break;
        UNSIGNED32 r = 0;
        if (AdsGetRecordNum(hCur, 0, &r) != 0) break;
        out.push_back(r);
        if (AdsSkip(hCur, 1) != 0) break;
    }
    return out;
}

}  // namespace

TEST_CASE("M10.6 SQL ORDER BY ascending walks rows in sorted order") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_asc";
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

    UNSIGNED8 sql[160] = "SELECT * FROM data.dbf ORDER BY TAG";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto tags = walk_tags(hCur);
    // Sorted data: AAAA, BBBB, CCCC, DDDD (source recnos were 2,4,1,3).
    REQUIRE(tags.size() == 4);
    CHECK(tags[0] == "AAAA");
    CHECK(tags[1] == "BBBB");
    CHECK(tags[2] == "CCCC");
    CHECK(tags[3] == "DDDD");
    // Materialised cursor: positional recnos 1..N, not source recnos.
    auto recs = walk_recnos(hCur);
    REQUIRE(recs.size() == 4);
    CHECK(recs[0] == 1);
    CHECK(recs[1] == 2);
    CHECK(recs[2] == 3);
    CHECK(recs[3] == 4);

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.6 SQL ORDER BY DESC reverses the order") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_desc";
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

    UNSIGNED8 sql[160] = "SELECT * FROM data.dbf ORDER BY TAG DESC";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto tags = walk_tags(hCur);
    REQUIRE(tags.size() == 4);
    CHECK(tags[0] == "DDDD");
    CHECK(tags[1] == "CCCC");
    CHECK(tags[2] == "BBBB");
    CHECK(tags[3] == "AAAA");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

namespace {
fs::path stage_two_col_dbf(const fs::path& dir) {
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
    hdr[4]  = 5;
    std::uint16_t hl = 32 + 32 * 2 + 1;
    std::uint16_t rl = 1 + 4 + 2;
    hdr[8]  = static_cast<std::uint8_t>( hl       & 0xFFu);
    hdr[9]  = static_cast<std::uint8_t>((hl >> 8) & 0xFFu);
    hdr[10] = static_cast<std::uint8_t>( rl       & 0xFFu);
    hdr[11] = static_cast<std::uint8_t>((rl >> 8) & 0xFFu);
    push(hdr.data(), hdr.size());
    auto fld = [&](const char* nm, std::uint8_t L) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), nm, 11);
        fd[11] = 'C'; fd[16] = L;
        push(fd.data(), fd.size());
    };
    fld("CITY", 4);
    fld("PR",   2);
    file.push_back(0x0D);
    auto rec = [&](const char* city, const char* pr) {
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(city)
                           ? static_cast<std::uint8_t>(city[i]) : ' ');
        for (int i = 0; i < 2; ++i)
            file.push_back(i < (int)std::strlen(pr)
                           ? static_cast<std::uint8_t>(pr[i]) : ' ');
    };
    rec("LON ", "20");                           // recno 1
    rec("LON ", "10");                           // recno 2
    rec("NYC ", "30");                           // recno 3
    rec("NYC ", "20");                           // recno 4
    rec("NYC ", "10");                           // recno 5
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}
}

TEST_CASE("M10.37 multi-column ORDER BY cascades on ties") {
    auto dir = fs::temp_directory_path() / "openads_m10_37";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_two_col_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // ORDER BY CITY ASC, PR DESC.
    // CITY=LON: PR 20,10 → DESC = 20 then 10
    // CITY=NYC: PR 30,20,10 → DESC = 30, 20, 10
    UNSIGNED8 sql[200] =
        "SELECT * FROM data.dbf ORDER BY CITY ASC, PR DESC";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    // Materialised cursor: positional recnos 1..5 in sort order.
    auto recs = walk_recnos(hCur);
    REQUIRE(recs.size() == 5);
    CHECK(recs[0] == 1);
    CHECK(recs[1] == 2);
    CHECK(recs[2] == 3);
    CHECK(recs[3] == 4);
    CHECK(recs[4] == 5);
    // Verify PR values follow DESC within each CITY.
    UNSIGNED8 fld_pr[8] = "PR";
    REQUIRE(AdsGotoTop(hCur) == 0);
    const char* expect_pr[] = {"20", "10", "30", "20", "10"};
    for (int i = 0; i < 5; ++i) {
        UNSIGNED8 buf[8] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hCur, fld_pr, buf, &cap, 0) == 0);
        std::string s(reinterpret_cast<char*>(buf), cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        CHECK(s == expect_pr[i]);
        if (i < 4) REQUIRE(AdsSkip(hCur, 1) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M10.6 SQL ORDER BY combines with WHERE") {
    auto dir = fs::temp_directory_path() / "openads_m10_6_where";
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

    // TAG > 'AAAA' filters to BBBB, CCCC, DDDD; ORDER BY TAG sorts them.
    UNSIGNED8 sql[200] =
        "SELECT * FROM data.dbf WHERE TAG > 'AAAA' ORDER BY TAG";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    auto tags = walk_tags(hCur);
    REQUIRE(tags.size() == 3);
    CHECK(tags[0] == "BBBB");
    CHECK(tags[1] == "CCCC");
    CHECK(tags[2] == "DDDD");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
