// The ORDER NUMBER the ACE API hands out for an ADI tag must be STABLE as
// more tags get added later.
//
// list_tags() derives ordinals from each tag's header page, which is
// allocated at end-of-file in creation order, so ordinal 1 is the first tag
// ever created regardless of the directory layout on disk. Reporting
// creation order once is not enough on its own, though: it has to keep
// holding as the tag count changes. Applications that resolve orders by
// number -- DBSETORDER(n) / OrdSetFocus(n), typical of a browse doing
// click-to-sort on a column -- keep those numbers in their own source. If a
// tag's ordinal shifted whenever a LATER tag was added, every such call site
// would point at a different order after the next reindex.
//
// This test creates 2 tags, reopens and records their ordinals, creates a
// 3rd, reopens again, and checks the first two ordinals did not move.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
struct AdiV2Scope {
    AdiV2Scope() {
#ifdef _WIN32
        _putenv_s("OPENADS_ADI_V2", "1");
#else
        setenv("OPENADS_ADI_V2", "1", 1);
#endif
    }
    ~AdiV2Scope() {
#ifdef _WIN32
        _putenv_s("OPENADS_ADI_V2", "");
#else
        unsetenv("OPENADS_ADI_V2");
#endif
    }
};

void set_c(ADSHANDLE h, const char* field, const char* val) {
    AdsSetString(h, (UNSIGNED8*)field, (UNSIGNED8*)val,
                 (UNSIGNED32)std::strlen(val));
}

ADSHANDLE make_tag(ADSHANDLE hTable, const char* bag, const char* tag,
                   const char* expr) {
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, (UNSIGNED8*)bag, (UNSIGNED8*)tag,
                             (UNSIGNED8*)expr, nullptr, nullptr, 0, 0,
                             &hIdx) == AE_SUCCESS);
    return hIdx;
}

std::string iname(ADSHANDLE h) {
    UNSIGNED8 nm[64]{}; UNSIGNED16 nl = 64;
    AdsGetIndexName(h, nm, &nl);
    return std::string(reinterpret_cast<const char*>(nm));
}
} // namespace

TEST_CASE("ADI ordinal number stays stable as later tags are added") {
    AdiV2Scope _v2;
    fs::path dir = fs::temp_directory_path() / "openads_adi_ordinal_stable";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[] = "ord.adt";
    UNSIGNED8 def[] = "CA,Character,4;CB,Character,4;CC,Character,4";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           def, &hT) == AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    set_c(hT, "CA", "A1"); set_c(hT, "CB", "B1"); set_c(hT, "CC", "C1");
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);

    std::string bag = (dir / "ord.adi").string();
    make_tag(hT, bag.c_str(), "TAGA", "CA");
    make_tag(hT, bag.c_str(), "TAGB", "CB");
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen, record ordinals 1 and 2 with only TAGA/TAGB on the bag.
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_EXCLUSIVE, &hT) == AE_SUCCESS);
    UNSIGNED8 b[260]{}; std::memcpy(b, bag.c_str(), bag.size());
    ADSHANDLE arr[16] = {0}; UNSIGNED16 alen = 16;
    REQUIRE(AdsOpenIndex(hT, b, arr, &alen) == AE_SUCCESS);
    REQUIRE(alen == 2);
    CHECK(iname(arr[0]) == "TAGA");   // ordinal 1
    CHECK(iname(arr[1]) == "TAGB");   // ordinal 2

    // Add a 3rd tag -- physically prepended (goes to directory slot 0).
    make_tag(hT, bag.c_str(), "TAGC", "CC");
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen again: TAGA/TAGB's ordinals must be EXACTLY what they were
    // before TAGC existed. TAGC takes ordinal 3 (it's the newest).
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_EXCLUSIVE, &hT) == AE_SUCCESS);
    ADSHANDLE arr2[16] = {0}; UNSIGNED16 alen2 = 16;
    REQUIRE(AdsOpenIndex(hT, b, arr2, &alen2) == AE_SUCCESS);
    REQUIRE(alen2 == 3);
    CHECK(iname(arr2[0]) == "TAGA");   // ordinal 1 -- unchanged
    CHECK(iname(arr2[1]) == "TAGB");   // ordinal 2 -- unchanged
    CHECK(iname(arr2[2]) == "TAGC");   // ordinal 3 -- the new one

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    AdsDisconnect(hConn);
    { std::error_code ec2; fs::remove_all(dir, ec2); }
}
