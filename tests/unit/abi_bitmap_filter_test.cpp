// M-BM.1 — Bitmap filter from recno array (BMDBFCDX compat).
// Tests AdsBmSetFilter, AdsBmGetFilterArray, AdsBmFilterAdd,
// AdsBmFilterDel, AdsBmSeekWild, and AdsBmTurbo.

#include "doctest.h"
#include "openads/ace.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_fixture(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_bm_abi_") + tag + ".dbf");
    fs::remove(p);

    constexpr std::uint16_t header_size = 32 + 32 + 32 + 1;
    constexpr std::uint16_t record_size = 1 + 5 + 3;

    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[1] = 124; hdr[2] = 1; hdr[3] = 31;
    hdr[4] = 4; hdr[5] = 0; hdr[6] = 0; hdr[7] = 0;
    hdr[8] = static_cast<std::uint8_t>(header_size & 0xFF);
    hdr[9] = static_cast<std::uint8_t>((header_size >> 8) & 0xFF);
    hdr[10] = static_cast<std::uint8_t>(record_size & 0xFF);
    hdr[11] = static_cast<std::uint8_t>((record_size >> 8) & 0xFF);
    file.insert(file.end(), hdr.begin(), hdr.end());

    auto push_field = [&](const char* name, char type,
                          std::uint8_t length) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), name, 11);
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = length;
        file.insert(file.end(), fd.begin(), fd.end());
    };
    push_field("NAME", 'C', 5);
    push_field("AGE",  'N', 3);
    file.push_back(0x0D);

    auto push_rec = [&](const char* name, const char* age) {
        file.push_back(' ');
        for (int i = 0; i < 5; ++i) {
            file.push_back(static_cast<std::uint8_t>(
                i < static_cast<int>(std::strlen(name)) ? name[i] : ' '));
        }
        std::string a(age);
        while (a.size() < 3) a.insert(a.begin(), ' ');
        for (char c : a) file.push_back(static_cast<std::uint8_t>(c));
    };
    push_rec("AAA", "25");
    push_rec("BBB", "42");
    push_rec("CCC", "30");
    push_rec("DDD", "18");
    push_rec("EEE", "55");
    file.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Helper: open table, return {hConn, hT}. Caller must close+disconnect.
struct TableHandle { ADSHANDLE hConn; ADSHANDLE hT; };
TableHandle open_table(const char* tag) {
    auto p = make_fixture(tag);
    auto dir = p.parent_path().string();
    auto base = p.filename().string();
    TableHandle th{0, 0};
    AdsConnect60(reinterpret_cast<UNSIGNED8*>(dir.data()),
                 ADS_LOCAL_SERVER, nullptr, nullptr,
                 ADS_DEFAULT, &th.hConn);
    AdsOpenTable(th.hConn,
                 reinterpret_cast<UNSIGNED8*>(base.data()),
                 nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                 &th.hT);
    return th;
}

void close_table(TableHandle& th) {
    if (th.hT) AdsCloseTable(th.hT);
    if (th.hConn) AdsDisconnect(th.hConn);
    th.hT = 0; th.hConn = 0;
}

// Helper: get all recnos from bitmap as sorted vector
std::vector<UNSIGNED32> get_all_recnos(ADSHANDLE hT) {
    UNSIGNED32 count = 0;
    AdsBmGetFilterArray(hT, nullptr, &count);
    std::vector<UNSIGNED32> v(count);
    if (count > 0) AdsBmGetFilterArray(hT, v.data(), &count);
    return v;
}

} // namespace

// =========================================================================
//  BASIC: create / read / clear
// =========================================================================

TEST_CASE("AdsBmSetFilter: create bitmap from recno array") {
    auto th = open_table("setfilter");

    std::array<UNSIGNED32, 3> recnos = {1, 3, 5};
    REQUIRE(AdsBmSetFilter(th.hT, recnos.data(),
        static_cast<UNSIGNED32>(recnos.size())) == 0);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 3);
    CHECK(v[2] == 5);

    // Navigation: GoTop -> 1, Skip -> 3, Skip -> 5, Skip -> EOF
    REQUIRE(AdsGotoTop(th.hT) == 0);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 1);

    AdsSkip(th.hT, 1);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 3);

    AdsSkip(th.hT, 1);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 5);

    AdsSkip(th.hT, 1);
    UNSIGNED16 eof = 0;
    AdsAtEOF(th.hT, &eof);
    CHECK(eof == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: empty array clears filter") {
    auto th = open_table("clear");

    std::array<UNSIGNED32, 2> recnos = {2, 4};
    AdsBmSetFilter(th.hT, recnos.data(),
        static_cast<UNSIGNED32>(recnos.size()));

    CHECK(get_all_recnos(th.hT).size() == 2);

    // Clear
    AdsBmSetFilter(th.hT, nullptr, 0);
    CHECK(get_all_recnos(th.hT).size() == 0);

    // Navigation sees all 5 records
    REQUIRE(AdsGotoTop(th.hT) == 0);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 1);
    AdsSkip(th.hT, 4);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 5);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: single record") {
    auto th = open_table("single");

    std::array<UNSIGNED32, 1> recnos = {3};
    AdsBmSetFilter(th.hT, recnos.data(), 1);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 1);
    CHECK(v[0] == 3);

    // GoTop lands on record 3
    AdsGotoTop(th.hT);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 3);

    // GoBottom also on record 3
    AdsGotoBottom(th.hT);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 3);

    // Skip -> EOF
    AdsSkip(th.hT, 1);
    UNSIGNED16 eof = 0;
    AdsAtEOF(th.hT, &eof);
    CHECK(eof == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: all records") {
    auto th = open_table("allrec");

    std::array<UNSIGNED32, 5> recnos = {1, 2, 3, 4, 5};
    AdsBmSetFilter(th.hT, recnos.data(), 5);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 5);

    // Full walk
    AdsGotoTop(th.hT);
    for (UNSIGNED32 i = 1; i <= 5; ++i) {
        UNSIGNED32 r = 0;
        AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
        CHECK(r == i);
        if (i < 5) AdsSkip(th.hT, 1);
    }

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: reverse order recnos") {
    auto th = open_table("reverse");

    // Recnos in descending order — should still produce sorted result
    std::array<UNSIGNED32, 3> recnos = {5, 2, 1};
    AdsBmSetFilter(th.hT, recnos.data(), 3);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 5);

    close_table(th);
}

// =========================================================================
//  ADD / DEL
// =========================================================================

TEST_CASE("AdsBmFilterAdd: add recnos to existing bitmap") {
    auto th = open_table("filteradd");

    std::array<UNSIGNED32, 2> initial = {1, 3};
    AdsBmSetFilter(th.hT, initial.data(), 2);

    std::array<UNSIGNED32, 1> add = {5};
    AdsBmFilterAdd(th.hT, add.data(), 1);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 1);
    CHECK(v[1] == 3);
    CHECK(v[2] == 5);

    close_table(th);
}

TEST_CASE("AdsBmFilterAdd: duplicates are ignored") {
    auto th = open_table("adddup");

    std::array<UNSIGNED32, 3> initial = {1, 3, 5};
    AdsBmSetFilter(th.hT, initial.data(), 3);

    // Add existing recno 3 + new recno 2
    std::array<UNSIGNED32, 2> add = {3, 2};
    AdsBmFilterAdd(th.hT, add.data(), 2);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 4);
    // Should be sorted: 1, 2, 3, 5
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
    CHECK(v[3] == 5);

    close_table(th);
}

TEST_CASE("AdsBmFilterAdd: no bitmap active installs fresh") {
    auto th = open_table("addnofilter");

    // No bitmap set yet — AdsBmFilterAdd should install fresh
    std::array<UNSIGNED32, 2> add = {2, 4};
    AdsBmFilterAdd(th.hT, add.data(), 2);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 2);
    CHECK(v[1] == 4);

    close_table(th);
}

TEST_CASE("AdsBmFilterDel: remove recnos from bitmap") {
    auto th = open_table("filterdel");

    std::array<UNSIGNED32, 3> initial = {1, 3, 5};
    AdsBmSetFilter(th.hT, initial.data(), 3);

    std::array<UNSIGNED32, 1> del = {3};
    AdsBmFilterDel(th.hT, del.data(), 1);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 5);

    close_table(th);
}

TEST_CASE("AdsBmFilterDel: remove all leaves empty") {
    auto th = open_table("delall");

    std::array<UNSIGNED32, 3> initial = {1, 3, 5};
    AdsBmSetFilter(th.hT, initial.data(), 3);

    std::array<UNSIGNED32, 3> del = {1, 3, 5};
    AdsBmFilterDel(th.hT, del.data(), 3);

    CHECK(get_all_recnos(th.hT).size() == 0);

    close_table(th);
}

TEST_CASE("AdsBmFilterDel: removing non-existent recno is no-op") {
    auto th = open_table("delnoop");

    std::array<UNSIGNED32, 2> initial = {1, 3};
    AdsBmSetFilter(th.hT, initial.data(), 2);

    // Remove recno 99 (doesn't exist in bitmap)
    std::array<UNSIGNED32, 1> del = {99};
    AdsBmFilterDel(th.hT, del.data(), 1);

    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 1);
    CHECK(v[1] == 3);

    close_table(th);
}

TEST_CASE("AdsBmFilterDel: no bitmap active is no-op") {
    auto th = open_table("delnofilter");

    // No bitmap set — AdsBmFilterDel should be a no-op
    std::array<UNSIGNED32, 1> del = {1};
    REQUIRE(AdsBmFilterDel(th.hT, del.data(), 1) == 0);

    // Still no bitmap
    CHECK(get_all_recnos(th.hT).size() == 0);

    close_table(th);
}

// =========================================================================
//  NAVIGATION with bitmap filter
// =========================================================================

TEST_CASE("AdsBmSetFilter: GoBottom lands on last filtered record") {
    auto th = open_table("gobottom");

    std::array<UNSIGNED32, 3> recnos = {2, 3, 5};
    AdsBmSetFilter(th.hT, recnos.data(), 3);

    AdsGotoBottom(th.hT);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 5);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip backward from BOF") {
    auto th = open_table("skipback");

    std::array<UNSIGNED32, 3> recnos = {1, 3, 5};
    AdsBmSetFilter(th.hT, recnos.data(), 3);

    AdsGotoTop(th.hT);
    // Skip backward from first filtered record -> BOF
    AdsSkip(th.hT, -1);
    UNSIGNED16 bof = 0;
    AdsAtBOF(th.hT, &bof);
    CHECK(bof == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip +2 walks two filtered records") {
    auto th = open_table("skip2");

    std::array<UNSIGNED32, 3> recnos = {1, 3, 5};
    AdsBmSetFilter(th.hT, recnos.data(), 3);

    AdsGotoTop(th.hT);
    AdsSkip(th.hT, 2);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 5);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip past EOF stays at EOF") {
    auto th = open_table("skipeof");

    std::array<UNSIGNED32, 2> recnos = {1, 3};
    AdsBmSetFilter(th.hT, recnos.data(), 2);

    AdsGotoTop(th.hT);
    AdsSkip(th.hT, 1);  // -> rec 3
    AdsSkip(th.hT, 1);  // -> EOF
    AdsSkip(th.hT, 1);  // should stay at EOF
    UNSIGNED16 eof = 0;
    AdsAtEOF(th.hT, &eof);
    CHECK(eof == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip -1 from middle filtered record") {
    auto th = open_table("skipmid");

    std::array<UNSIGNED32, 3> recnos = {1, 3, 5};
    AdsBmSetFilter(th.hT, recnos.data(), 3);

    AdsGotoTop(th.hT);
    AdsSkip(th.hT, 1);  // -> rec 3
    AdsSkip(th.hT, -1); // -> rec 1
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip large delta past EOF") {
    auto th = open_table("skiplarge");

    std::array<UNSIGNED32, 2> recnos = {2, 4};
    AdsBmSetFilter(th.hT, recnos.data(), 2);

    AdsGotoTop(th.hT);
    AdsSkip(th.hT, 100);  // way past EOF
    UNSIGNED16 eof = 0;
    AdsAtEOF(th.hT, &eof);
    CHECK(eof == 1);

    close_table(th);
}

TEST_CASE("AdsBmSetFilter: Skip -large from first record -> BOF") {
    auto th = open_table("skipneg");

    std::array<UNSIGNED32, 2> recnos = {2, 4};
    AdsBmSetFilter(th.hT, recnos.data(), 2);

    AdsGotoTop(th.hT);
    AdsSkip(th.hT, -100);  // way past BOF
    UNSIGNED16 bof = 0;
    AdsAtBOF(th.hT, &bof);
    CHECK(bof == 1);

    close_table(th);
}

// =========================================================================
//  COMBINED add/del sequences
// =========================================================================

TEST_CASE("AdsBm: add then del sequence") {
    auto th = open_table("adddel");

    // Start {1}
    std::array<UNSIGNED32, 1> s = {1};
    AdsBmSetFilter(th.hT, s.data(), 1);

    // Add {3, 5} -> {1, 3, 5}
    std::array<UNSIGNED32, 2> a1 = {3, 5};
    AdsBmFilterAdd(th.hT, a1.data(), 2);
    CHECK(get_all_recnos(th.hT).size() == 3);

    // Del {3} -> {1, 5}
    std::array<UNSIGNED32, 1> d1 = {3};
    AdsBmFilterDel(th.hT, d1.data(), 1);
    CHECK(get_all_recnos(th.hT).size() == 2);

    // Add {2, 4} -> {1, 2, 4, 5}
    std::array<UNSIGNED32, 2> a2 = {2, 4};
    AdsBmFilterAdd(th.hT, a2.data(), 2);
    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 4);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 4);
    CHECK(v[3] == 5);

    // Del {1, 5} -> {2, 4}
    std::array<UNSIGNED32, 2> d2 = {1, 5};
    AdsBmFilterDel(th.hT, d2.data(), 2);
    v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 2);
    CHECK(v[1] == 4);

    close_table(th);
}

TEST_CASE("AdsBm: set filter replaces previous") {
    auto th = open_table("replace");

    std::array<UNSIGNED32, 3> r1 = {1, 2, 3};
    AdsBmSetFilter(th.hT, r1.data(), 3);
    CHECK(get_all_recnos(th.hT).size() == 3);

    // New set replaces old
    std::array<UNSIGNED32, 2> r2 = {4, 5};
    AdsBmSetFilter(th.hT, r2.data(), 2);
    auto v = get_all_recnos(th.hT);
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 4);
    CHECK(v[1] == 5);

    close_table(th);
}

// =========================================================================
//  EDGE CASES
// =========================================================================

TEST_CASE("AdsBmSetFilter: recno beyond record count extends bitmap") {
    auto th = open_table("beyond");

    // Table has 5 records; recno 99 extends the bitmap to accommodate it
    std::array<UNSIGNED32, 2> recnos = {2, 99};
    AdsBmSetFilter(th.hT, recnos.data(), 2);

    auto v = get_all_recnos(th.hT);
    // Both recnos are present; bitmap grows to hold recno 99
    REQUIRE(v.size() == 2);
    CHECK(v[0] == 2);
    CHECK(v[1] == 99);

    close_table(th);
}

TEST_CASE("AdsBmGetFilterArray: no bitmap returns count=0") {
    auto th = open_table("nobitmap");

    UNSIGNED32 count = 999;
    AdsBmGetFilterArray(th.hT, nullptr, &count);
    CHECK(count == 0);

    close_table(th);
}

TEST_CASE("AdsBmFilterAdd: with empty array is no-op") {
    auto th = open_table("addempty");

    std::array<UNSIGNED32, 2> initial = {1, 3};
    AdsBmSetFilter(th.hT, initial.data(), 2);

    // Add empty
    AdsBmFilterAdd(th.hT, nullptr, 0);
    CHECK(get_all_recnos(th.hT).size() == 2);

    close_table(th);
}

TEST_CASE("AdsBmFilterDel: with empty array is no-op") {
    auto th = open_table("delempty");

    std::array<UNSIGNED32, 2> initial = {1, 3};
    AdsBmSetFilter(th.hT, initial.data(), 2);

    // Del empty
    AdsBmFilterDel(th.hT, nullptr, 0);
    CHECK(get_all_recnos(th.hT).size() == 2);

    close_table(th);
}

// =========================================================================
//  TURBO MODE
// =========================================================================

TEST_CASE("AdsBmTurbo: enable/disable round-trip") {
    auto th = open_table("turbo");

    REQUIRE(AdsBmTurbo(th.hT, 1) == 0);

    // Table works normally in turbo mode
    AdsGotoTop(th.hT);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 1);

    AdsGotoBottom(th.hT);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 5);

    // Disable turbo
    REQUIRE(AdsBmTurbo(th.hT, 0) == 0);

    AdsGotoTop(th.hT);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 1);

    close_table(th);
}

TEST_CASE("AdsBmTurbo: with active bitmap filter") {
    auto th = open_table("turbobm");

    // Install bitmap
    std::array<UNSIGNED32, 2> recnos = {2, 4};
    AdsBmSetFilter(th.hT, recnos.data(), 2);

    // Enable turbo
    AdsBmTurbo(th.hT, 1);

    // Bitmap navigation still works
    AdsGotoTop(th.hT);
    UNSIGNED32 r = 0;
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 2);

    AdsSkip(th.hT, 1);
    AdsGetRecordNum(th.hT, ADS_IGNOREFILTERS, &r);
    CHECK(r == 4);

    AdsSkip(th.hT, 1);
    UNSIGNED16 eof = 0;
    AdsAtEOF(th.hT, &eof);
    CHECK(eof == 1);

    AdsBmTurbo(th.hT, 0);
    close_table(th);
}

// =========================================================================
//  WILDCARD SEEK (stub — returns success, validates no crash)
// =========================================================================

TEST_CASE("AdsBmSeekWild: stub returns success with no index") {
    auto th = open_table("wildnoindex");

    UNSIGNED8 pattern[] = "A*";
    ADSHANDLE hResult = 0;
    // No index open — should fail gracefully
    UNSIGNED32 rc = AdsBmSeekWild(th.hT, pattern, 0, 0, 0, 0, &hResult);
    CHECK(rc != 0);  // should return error

    close_table(th);
}

TEST_CASE("AdsBmSeekWild: stub with index returns success") {
    auto th = open_table("wildidx");

    // Create an index on NAME (need a valid filename)
    auto idx_path = fs::temp_directory_path() / "openads_bm_abi_wildidx.cdx";
    std::error_code remove_ec;
    fs::remove(idx_path, remove_ec);  // best-effort: AV scanners can
                                      // hold the file briefly on CI
    std::string idx_file = idx_path.string();
    UNSIGNED8 tag[] = "NAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    AdsCreateIndex61(th.hT,
                     reinterpret_cast<UNSIGNED8*>(idx_file.data()),
                     tag, expr, nullptr, nullptr,
                     ADS_DEFAULT, ADS_DEFAULT, &hIdx);

    UNSIGNED8 pattern[] = "A*";
    ADSHANDLE hResult = 0;
    // With index open — stub returns success
    UNSIGNED32 rc = AdsBmSeekWild(th.hT, pattern, 0, 0, 0, 0, &hResult);
    CHECK(rc == 0);

    close_table(th);
}

// =========================================================================
//  MULTIPLE TABLES
// =========================================================================

TEST_CASE("AdsBm: independent bitmaps on two tables") {
    auto p1 = make_fixture("multi1");
    auto p2 = make_fixture("multi2");
    auto d1 = p1.parent_path().string();
    auto d2 = p2.parent_path().string();
    auto b1 = p1.filename().string();
    auto b2 = p2.filename().string();

    ADSHANDLE hC1 = 0, hC2 = 0;
    AdsConnect60(reinterpret_cast<UNSIGNED8*>(d1.data()),
                 ADS_LOCAL_SERVER, nullptr, nullptr, ADS_DEFAULT, &hC1);
    AdsConnect60(reinterpret_cast<UNSIGNED8*>(d2.data()),
                 ADS_LOCAL_SERVER, nullptr, nullptr, ADS_DEFAULT, &hC2);

    ADSHANDLE hT1 = 0, hT2 = 0;
    AdsOpenTable(hC1, reinterpret_cast<UNSIGNED8*>(b1.data()),
                 nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT1);
    AdsOpenTable(hC2, reinterpret_cast<UNSIGNED8*>(b2.data()),
                 nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, &hT2);

    // Table 1: {1, 3}
    std::array<UNSIGNED32, 2> r1 = {1, 3};
    AdsBmSetFilter(hT1, r1.data(), 2);

    // Table 2: {2, 4, 5}
    std::array<UNSIGNED32, 3> r2 = {2, 4, 5};
    AdsBmSetFilter(hT2, r2.data(), 3);

    // Verify independence
    CHECK(get_all_recnos(hT1).size() == 2);
    CHECK(get_all_recnos(hT2).size() == 3);

    AdsCloseTable(hT1);
    AdsCloseTable(hT2);
    AdsDisconnect(hC1);
    AdsDisconnect(hC2);
}
