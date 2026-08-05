#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// A single-table SELECT ... ORDER BY (and DISTINCT / LIMIT) returns a
// MATERIALISED static cursor (ADS_CDX semantics): its own temp table, isolated
// from the source, with its own recnos 1..N in result order. The ERP browses
// these in a TXBrowse (e.g. BuscaRegistro: "Select * From [articulo.dat] WHERE
// UPPER(cnombreart) LIKE 'X%' ORDER BY cnombreart") and then runs INDEX ON /
// DBSETORDER on the result — which must hit the TEMP, never the production
// table's official .cdx. This test pins that the materialised cursor browses
// cleanly: GOTO-then-SKIP walks in result order, OrdKeyNo / KeyCount / RelKeyPos
// and bookmark round-trip all agree (recno == position on the static cursor).

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
    hdr[4]  = 5;                 // record count
    hdr[8]  = 32 + 32 + 1;       // header length
    hdr[10] = 1 + 4;             // record length
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    auto rec = [&](const char* s) {
        file.push_back(' ');                       // not-deleted flag
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(s)
                           ? static_cast<std::uint8_t>(s[i]) : ' ');
    };
    // recno: 1=CCCC 2=AAAA 3=DDDD 4=BBBB 5=EEEE  (sorted order != recno order)
    rec("CCCC"); rec("AAAA"); rec("DDDD"); rec("BBBB"); rec("EEEE");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::string field(ADSHANDLE h, const char* f) {
    UNSIGNED8 buf[32] = {0};
    UNSIGNED32 cap = sizeof(buf);
    UNSIGNED8 fld[16] = {0};
    std::memcpy(fld, f, std::strlen(f) + 1);
    if (AdsGetField(h, fld, buf, &cap, 0) != 0) return {};
    std::string s(reinterpret_cast<char*>(buf), cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("SQL single-table ORDER BY result: browse nav stays in sync") {
    auto dir = fs::temp_directory_path() / "openads_sql_temp_browse_nav";
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

    // WHERE drops AAAA (recno 2). Sorted visible set: BBBB,CCCC,DDDD,EEEE.
    UNSIGNED8 sql[200] =
        "SELECT * FROM data.dbf WHERE TAG >= 'BBBB' ORDER BY TAG";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);

    // The ORDER BY result is a materialised static cursor (ADS_CDX semantics),
    // so it has its OWN recnos 1..N in result order — not the source recnos.
    const UNSIGNED32 exp_recno[4] = {1, 2, 3, 4};
    const char*      exp_val[4]   = {"BBBB", "CCCC", "DDDD", "EEEE"};

    // Counts reflect the visible (filtered+sorted) set, not the full table.
    UNSIGNED32 rcount = 0;
    REQUIRE(AdsGetRecordCount(hCur, 0, &rcount) == 0);
    CHECK(rcount == 4u);
    UNSIGNED32 kcount = 0;
    REQUIRE(AdsGetKeyCount(hCur, 0, &kcount) == 0);
    CHECK(kcount == 4u);

    // Walk top->EOF: sorted values in order; OrdKeyNo = 1..N; recno = source.
    REQUIRE(AdsGotoTop(hCur) == 0);
    for (int i = 0; i < 4; ++i) {
        UNSIGNED32 rn = 0, kn = 0;
        REQUIRE(AdsGetRecordNum(hCur, 0, &rn) == 0);
        REQUIRE(AdsGetKeyNum(hCur, 0, &kn) == 0);
        CHECK(rn == exp_recno[i]);
        CHECK(kn == static_cast<UNSIGNED32>(i + 1));
        CHECK(field(hCur, "TAG") == exp_val[i]);
        if (i < 3) REQUIRE(AdsSkip(hCur, 1) == 0);
    }
    UNSIGNED16 eof = 0;
    REQUIRE(AdsSkip(hCur, 1) == 0);
    REQUIRE(AdsAtEOF(hCur, &eof) == 0);
    CHECK(eof == 1);

    // THE BUG: land on a row by recno (bookmark restore), then SKIP must
    // advance in SORTED order and read the right row — not jump to a physical
    // neighbour. OrdKeyNo at the landed row is the sorted position, not recno.
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsGotoRecord(hCur, exp_recno[i]) == 0);
        UNSIGNED32 kn = 0;
        REQUIRE(AdsGetKeyNum(hCur, 0, &kn) == 0);
        CHECK(kn == static_cast<UNSIGNED32>(i + 1));
        CHECK(field(hCur, "TAG") == exp_val[i]);
        REQUIRE(AdsSkip(hCur, 1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hCur, 0, &rn) == 0);
        CHECK(rn == exp_recno[i + 1]);
        CHECK(field(hCur, "TAG") == exp_val[i + 1]);
    }

    // RelKeyPos: first sorted row -> 0.0, last -> 1.0 (scrollbar thumb).
    REQUIRE(AdsGotoRecord(hCur, exp_recno[0]) == 0);
    double p0 = -1.0;
    REQUIRE(AdsGetRelKeyPos(hCur, &p0) == 0);
    CHECK(p0 == doctest::Approx(0.0));
    REQUIRE(AdsGotoRecord(hCur, exp_recno[3]) == 0);
    double p3 = -1.0;
    REQUIRE(AdsGetRelKeyPos(hCur, &p3) == 0);
    CHECK(p3 == doctest::Approx(1.0));

    // SetRelKeyPos (thumb drag): fraction -> sorted position, then SKIP keeps
    // walking the sequence.
    REQUIRE(AdsSetRelKeyPos(hCur, 0.0) == 0);
    CHECK(field(hCur, "TAG") == "BBBB");
    REQUIRE(AdsSetRelKeyPos(hCur, 1.0) == 0);
    CHECK(field(hCur, "TAG") == "EEEE");

    // Bookmark round-trip on a middle row (DDDD, sorted pos 3 / source recno 3).
    REQUIRE(AdsGotoRecord(hCur, 3) == 0);
    UNSIGNED8 bm[16] = {0};
    UNSIGNED32 bmlen = sizeof(bm);
    REQUIRE(AdsGetBookmark60(hCur, bm, &bmlen) == 0);
    REQUIRE(AdsGotoTop(hCur) == 0);
    REQUIRE(AdsGotoBookmark60(hCur, bm, bmlen) == 0);
    UNSIGNED32 rn = 0, kn = 0;
    REQUIRE(AdsGetRecordNum(hCur, 0, &rn) == 0);
    REQUIRE(AdsGetKeyNum(hCur, 0, &kn) == 0);
    CHECK(rn == 3u);
    CHECK(kn == 3u);
    CHECK(field(hCur, "TAG") == "DDDD");

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
