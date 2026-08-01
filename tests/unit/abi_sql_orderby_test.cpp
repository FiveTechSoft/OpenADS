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

// ---------------------------------------------------------------------------
// Column names longer than 10 characters must survive the static-cursor
// materialisation that ORDER BY / DISTINCT / LIMIT / TOP trigger (#136/#146).
//
// The temp table those paths build used to be created with ADS_CDX, i.e. a DBF,
// whose on-disk field descriptor is 11 bytes — so every name was silently cut
// to 10 and `RI_Primary_Table` came back as `RI_Primary`. That is not a
// cosmetic difference: it breaks SAP parity on the system.* catalogs (SAP's own
// names run to 18 chars) and mangles any user table with long columns.
// The temp is now ADS_ADT, which carries full-length names.
//
// If this test fails with a truncated name, something re-introduced a DBF-format
// temp on a materialising path. Do not "fix" the expectation.
// ---------------------------------------------------------------------------
TEST_CASE("materialised cursors keep column names longer than 10 chars") {
    auto dir = fs::temp_directory_path() / "openads_sql_longcol";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto ds = dir.string();
    std::memcpy(srv, ds.c_str(), ds.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    // An ADT source so the long names exist before the query runs; 17 and 16
    // characters, both past DBF's limit and both real SAP catalog widths.
    // The N(12,2) column is deliberate: the temp format has to carry a long
    // name AND the declared scale at the same time. ADT DOUBLE can do the
    // first but not the second (its descriptor stores no decimal count), so
    // this pins the ASCII-numeric mapping the materialiser relies on.
    UNSIGNED8 tname[32] = "longcols";
    // AsciiNumeric (ADT type 2) rather than plain Numeric: on ADT, "Numeric"
    // with decimals becomes a binary DOUBLE, and AdsSetString into an ADT
    // DOUBLE currently stores the text verbatim instead of parsing it — a
    // separate defect that would make this fixture, not the code under test,
    // the thing that fails.
    UNSIGNED8 defs[160] = "User_Defined_Prop,Character,20;"
                          "RI_Primary_Table,Character,20;"
                          "Trig_Priority_Num,AsciiNumeric,12,2";
    ADSHANDLE hNew = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_ADT, 0, 0, 0, 0,
                           defs, &hNew) == 0);
    // Rows matter: with an empty source the materialising path is skipped and
    // the truncation never shows up.
    {
        UNSIGNED8 f1[32] = "User_Defined_Prop";
        UNSIGNED8 f2[32] = "RI_Primary_Table";
        UNSIGNED8 f3[32] = "Trig_Priority_Num";
        UNSIGNED8 num[16] = "10.50";
        for (const char* v : {"bbb", "aaa", "ccc"}) {
            REQUIRE(AdsAppendRecord(hNew) == 0);
            UNSIGNED8 buf[32]{};
            std::memcpy(buf, v, std::strlen(v));
            REQUIRE(AdsSetString(hNew, f1, buf,
                        static_cast<UNSIGNED32>(std::strlen(v))) == 0);
            REQUIRE(AdsSetString(hNew, f2, buf,
                        static_cast<UNSIGNED32>(std::strlen(v))) == 0);
            REQUIRE(AdsSetString(hNew, f3, num, 5) == 0);
            REQUIRE(AdsWriteRecord(hNew) == 0);
        }
    }
    REQUIRE(AdsCloseTable(hNew) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // Each of these takes the materialising path; a plain SELECT does not.
    const std::string cols =
        "User_Defined_Prop, RI_Primary_Table, Trig_Priority_Num";
    const std::vector<std::string> queries = {
        "SELECT " + cols + " FROM longcols ORDER BY RI_Primary_Table",
        "SELECT DISTINCT " + cols + " FROM longcols",
        "SELECT TOP 1 " + cols + " FROM longcols",
    };
    for (const std::string& q : queries) {
        UNSIGNED8 sql[256]{};
        std::memcpy(sql, q.c_str(), q.size() + 1);
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);

        // Read the names back off the cursor: this is what a SAP-compatible
        // client sees, and what `SELECT ... WHERE <name>` has to match.
        UNSIGNED16 count = 0;
        REQUIRE(AdsGetNumFields(hCur, &count) == 0);
        REQUIRE(count == 3);
        for (UNSIGNED16 i = 1; i <= count; ++i) {
            UNSIGNED8  nm[128] = {};
            UNSIGNED16 nlen = sizeof(nm) - 1;
            REQUIRE(AdsGetFieldName(hCur, i, nm, &nlen) == 0);
            std::string got(reinterpret_cast<const char*>(nm), nlen);
            while (!got.empty() && (got.back() == ' ' || got.back() == '\0'))
                got.pop_back();
            CAPTURE(q);
            CAPTURE(got);
            CHECK((got == "User_Defined_Prop" || got == "RI_Primary_Table" ||
                   got == "Trig_Priority_Num"));
        }

        // ...and the declared scale survives alongside the long name. Losing
        // this means the temp went back to a format that cannot carry both.
        REQUIRE(AdsGotoTop(hCur) == 0);
        {
            UNSIGNED8  fld[32] = "Trig_Priority_Num";
            UNSIGNED8  vb[64]  = {};
            UNSIGNED32 vlen    = sizeof(vb);
            REQUIRE(AdsGetString(hCur, fld, vb, &vlen, 0) == 0);
            std::string sv(reinterpret_cast<const char*>(vb), vlen);
            while (!sv.empty() && sv.back()  == ' ') sv.pop_back();
            while (!sv.empty() && sv.front() == ' ') sv.erase(sv.begin());
            CAPTURE(q);
            CAPTURE(sv);
            CHECK(sv == "10.50");
        }
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
