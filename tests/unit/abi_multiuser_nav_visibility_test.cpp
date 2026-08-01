// Pritpal Bedi (30/07/2026): every station shows the same LastRec after a
// peer append, but Browse / Skip only walks the rows that existed when THIS
// workarea was opened. Root cause: CdxDriver cached rec_count_ at open and
// natural-order Skip used that fence; GetRecordCount (local) did not re-read
// the header either. With an order open, the CDX page cache also lagged peer
// bag writes until the sub-tag counter was rechecked.
//
// This test opens two Shared connections on the same table: B appends, A
// must see the new rows via GetRecordCount, GotoTop+Skip (natural), and
// ordered walk after B maintains the production bag.
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    UNSIGNED8 srv[512] = {};
    const auto s = dir.string();
    REQUIRE(s.size() + 1 <= sizeof(srv));
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

void append_name(ADSHANDLE hTbl, const char* name) {
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                         static_cast<UNSIGNED32>(std::strlen(name))) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);
}

int walk_count(ADSHANDLE hTbl, ADSHANDLE hOrd) {
    REQUIRE(AdsGotoTop(hOrd ? hOrd : hTbl) == 0);
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
        ++n;
        REQUIRE(AdsSkip(hOrd ? hOrd : hTbl, 1) == 0);
        if (n > 10000) break;
    }
    return n;
}

}  // namespace

TEST_CASE("multiuser: peer append is visible to natural-order browse") {
    auto dir = fs::temp_directory_path() / "openads_mu_nav_natural";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto h1 = connect(dir);
    auto h2 = connect(dir);

    UNSIGNED8 flds[] = "NAME,C,20,0";
    UNSIGNED8 nm[]   = "T";
    ADSHANDLE t1 = 0;
    REQUIRE(AdsCreateTable(h1, nm, nm, ADS_CDX, ADS_ANSI, 0, ADS_SHARED, 0,
                           flds, &t1) == 0);
    append_name(t1, "R1");
    append_name(t1, "R2");
    append_name(t1, "R3");
    REQUIRE(AdsCloseTable(t1) == 0);
    t1 = 0;

    // A opens with 3 rows.
    REQUIRE(AdsOpenTable(h1, nm, nm, ADS_CDX, 1, ADS_SHARED, 0, 1, &t1) == 0);
    UNSIGNED32 c = 0;
    REQUIRE(AdsGetRecordCount(t1, 0, &c) == 0);
    CHECK(c == 3u);
    CHECK(walk_count(t1, 0) == 3);

    // B opens and appends two more.
    ADSHANDLE t2 = 0;
    REQUIRE(AdsOpenTable(h2, nm, nm, ADS_CDX, 1, ADS_SHARED, 0, 1, &t2) == 0);
    append_name(t2, "N1");
    append_name(t2, "N2");
    c = 0;
    REQUIRE(AdsGetRecordCount(t2, 0, &c) == 0);
    CHECK(c == 5u);

    // A must now report 5 and walk all five (the bug: count and walk stuck at 3).
    c = 0;
    REQUIRE(AdsGetRecordCount(t1, 0, &c) == 0);
    CHECK(c == 5u);
    CHECK(walk_count(t1, 0) == 5);

    REQUIRE(AdsCloseTable(t1) == 0);
    REQUIRE(AdsCloseTable(t2) == 0);
    REQUIRE(AdsDisconnect(h1) == 0);
    REQUIRE(AdsDisconnect(h2) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("multiuser: peer append with production CDX is visible on ordered browse") {
    auto dir = fs::temp_directory_path() / "openads_mu_nav_index";
    std::error_code ec;
    fs::remove_all(dir, ec);

    auto h1 = connect(dir);
    auto h2 = connect(dir);

    UNSIGNED8 flds[] = "NAME,C,20,0";
    UNSIGNED8 nm[]   = "T";
    ADSHANDLE t1 = 0;
    REQUIRE(AdsCreateTable(h1, nm, nm, ADS_CDX, ADS_ANSI, 0, ADS_SHARED, 0,
                           flds, &t1) == 0);
    append_name(t1, "Charlie");
    append_name(t1, "Alice");
    append_name(t1, "Bob");
    // Production bag while table still open on A.
    UNSIGNED8 bag[]  = "T.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(t1, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(t1) == 0);

    // Both stations open table (production CDX auto-open) + explicit bag.
    REQUIRE(AdsOpenTable(h1, nm, nm, ADS_CDX, 1, ADS_SHARED, 0, 1, &t1) == 0);
    ADSHANDLE t2 = 0;
    REQUIRE(AdsOpenTable(h2, nm, nm, ADS_CDX, 1, ADS_SHARED, 0, 1, &t2) == 0);

    std::array<ADSHANDLE, 4> a1{}, a2{};
    UNSIGNED16 n1 = 4, n2 = 4;
    REQUIRE(AdsOpenIndex(t1, bag, a1.data(), &n1) == 0);
    REQUIRE(AdsOpenIndex(t2, bag, a2.data(), &n2) == 0);
    REQUIRE(n1 >= 1u);
    REQUIRE(n2 >= 1u);

    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(a1[0], ADS_IGNOREFILTERS, &kc) == 0);
    CHECK(kc == 3u);
    CHECK(walk_count(t1, a1[0]) == 3);

    // B appends with order open → bag grows.
    append_name(t2, "Diana");
    REQUIRE(AdsGetKeyCount(a2[0], ADS_IGNOREFILTERS, &kc) == 0);
    CHECK(kc == 4u);

    // A must see 4 keys after peer write (reload CDX header/counter).
    REQUIRE(AdsGetKeyCount(a1[0], ADS_IGNOREFILTERS, &kc) == 0);
    CHECK(kc == 4u);
    CHECK(walk_count(t1, a1[0]) == 4);

    // Ordered top should still be Alice after Diana is added.
    REQUIRE(AdsGotoTop(a1[0]) == 0);
    UNSIGNED8 buf[32] = {};
    UNSIGNED32 bl = sizeof(buf);
    REQUIRE(AdsGetString(t1, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
                         buf, &bl, ADS_NONE) == 0);
    std::string name(reinterpret_cast<char*>(buf));
    while (!name.empty() && name.back() == ' ') name.pop_back();
    CHECK(name == "Alice");

    REQUIRE(AdsCloseTable(t1) == 0);
    REQUIRE(AdsCloseTable(t2) == 0);
    REQUIRE(AdsDisconnect(h1) == 0);
    REQUIRE(AdsDisconnect(h2) == 0);
    fs::remove_all(dir, ec);
}
