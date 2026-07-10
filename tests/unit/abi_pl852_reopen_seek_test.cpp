// Regression for issue #130 follow-up (v1.8.6): NTXPL852 collation seek
// broke on reopened CDX bags.
//
// Root cause chain:
//   - mark_cdx_key_encoding() (v1.8.5) marks ANY reopened CDX tag whose
//     key_length() == 8 as FoxNumeric ("8-byte key => numeric key by
//     convention") — including plain C(8) character tags.
//   - v1.8.6 made key_encoding() select the B+tree comparator
//     (FoxNumeric/NtxNumeric -> memcmp, Text -> OEM collation table).
//   - A C(8) tag built under PL852 ordering but reopened as "FoxNumeric"
//     therefore descends the tree with memcmp and misses keys whose
//     PL852 weight order differs from raw byte order (Ł = 0x9D sorts
//     between L and M, but byte-wise sorts after Z).
//
// The existing PL852 tests never reopen the bag, which is why the suite
// stayed green while real-world (open existing production CDX) seeks
// failed.
#include "doctest.h"
#include "fixtures/polish_oem_fixture.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Rows in PL852 order: LAC < ŁAB (0x9D) < MAD < ZBY. Byte-wise, 0x9D
// sorts after 'Z', so a memcmp tree walk diverges from the PL852 one.
const char* kRow0 = "LACAAAAA";
const char* kRow2 = "MADCCCCC";
const char* kRow3 = "ZBYDDDDD";

void seek_and_expect(ADSHANDLE hI, ADSHANDLE hT, const char* key,
                     const char* expect_prefix) {
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI,
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(key)),
        static_cast<UNSIGNED16>(std::strlen(key)),
        ADS_STRINGKEY, /*hard*/ 0, &found) == 0);
    INFO("seek [" << key << "] found=" << found);
    CHECK(found == 1);
    if (found == 1) {
        UNSIGNED8 got[32];
        UNSIGNED32 got_len = sizeof(got);
        UNSIGNED8 fName[] = "NAME";
        REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
        CHECK(std::memcmp(got, expect_prefix,
                          std::strlen(expect_prefix)) == 0);
    }
}

} // namespace

TEST_CASE("NTXPL852 C(8) tag survives close + reopen (klen==8 heuristic)") {
    auto dir = fs::temp_directory_path() / "openads_pl852_reopen";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 coll[] = "NTXPL852";
    REQUIRE(AdsSetCollation(hConn, coll) == 0);

    UNSIGNED8 def[]   = "NAME,C,8,0";
    UNSIGNED8 tname[] = "plreo";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    const char* rows[] = {
        kRow0, openads::test::kPolishLabRow8, kRow2, kRow3 };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "plreo.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Sanity: seeks work on the freshly created index.
    seek_and_expect(hI, hT, kRow2, "MAD");
    seek_and_expect(hI, hT, openads::test::kPolishLabRow8, "\x9D""AB");

    // Close and REOPEN — the production-CDX path every real app takes.
    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0; hI = 0;
    UNSIGNED8 tfile[] = "plreo.dbf";
    UNSIGNED8 alias[] = "plreo";
    REQUIRE(AdsOpenTable(hConn, tfile, alias, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == 0);

    // Regression: with the tag mis-marked FoxNumeric on reopen and the
    // v1.8.6 memcmp comparator, both of these missed.
    seek_and_expect(hI, hT, kRow2, "MAD");
    seek_and_expect(hI, hT, openads::test::kPolishLabRow8, "\x9D""AB");
    seek_and_expect(hI, hT, kRow3, "ZBY");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("NTXPL852 Upper() tag survives close + reopen") {
    auto dir = fs::temp_directory_path() / "openads_pl852_reopen_up";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 coll[] = "NTXPL852";
    REQUIRE(AdsSetCollation(hConn, coll) == 0);

    // Wider than 8 so the klen==8 heuristic is NOT in play; this case
    // pins the Upper()+collation path itself across a reopen.
    UNSIGNED8 def[]   = "NAME,C,20,0";
    UNSIGNED8 tname[] = "plreup";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    // Mixed-case rows; ł = 0x88 upper-cases to Ł = 0x9D under PL852.
    const char lab_lower[] = {'\x88', 'a', 'b', 'b', 'b', '\0'};
    const char* rows[] = { "Lacaaaaa", lab_lower, "Madccccc", "Zbydddd" };
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "plreup.cdx";
    UNSIGNED8 tag[]  = "BYUP";
    UNSIGNED8 expr[] = "Upper(NAME)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Harbour-side Upper() under CP852 produces these seek keys.
    const char lab_upper[] = {'\x9D', 'A', 'B', 'B', 'B', '\0'};
    seek_and_expect(hI, hT, "MADCCCCC", "Mad");
    seek_and_expect(hI, hT, lab_upper, "\x88""ab");

    REQUIRE(AdsCloseTable(hT) == 0);
    hT = 0; hI = 0;
    UNSIGNED8 tfile[] = "plreup.dbf";
    UNSIGNED8 alias[] = "plreup";
    REQUIRE(AdsOpenTable(hConn, tfile, alias, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == 0);

    seek_and_expect(hI, hT, "MADCCCCC", "Mad");
    seek_and_expect(hI, hT, lab_upper, "\x88""ab");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
