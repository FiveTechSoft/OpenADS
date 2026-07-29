// Regression: every tag but the first vanished from a large .ADI bag.
//
// Reported by RusSoft (2026-07-28). Their ERP indexes ARTICULO with 22 tags.
// After reindexing an ADT company the bag held all 22 (the tag directory on
// page 2 says count=22 and the 22 per-tag headers are on disk), yet reopening
// it exposed exactly ONE order. The browse then asked to sort by a tag the
// engine claimed not to exist, and the ERP fell back to scanning 34,595
// records on every open — a 12-second screen.
//
// Root cause: the tag-directory entry stored the per-tag header PAGE NUMBER
// in a single byte. The first tag always lands on page 3, so it survived.
// Every later add_tag appends its pages at the end of a bag that is already
// megabytes long, so the real page (897, 5282, 13519, …) was truncated to its
// low byte. On reopen the reader jumped to the wrong page, found no F-marker
// there, and dropped the tag without a word.
//
// Fix: the page is written and read as u32 LE over the entry's first four
// bytes (they were unused zeros, so old bags still read back identically).
//
// This test forces the bag past page 255 by loading enough records before the
// extra tags are added — that is the only condition under which the old code
// failed, which is why the existing two-tag test on a tiny table passed.

#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("ADI: tags survive when the bag grows past page 255") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_tagdir_wide";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "wide.adt";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10;CNOMBRE,Character,40;CGRUPO,Character,6";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // Enough rows that the index pages for tags 2..N land well past page 255.
    const std::uint32_t kRows = 4000;
    for (std::uint32_t i = 0; i < kRows; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char cod[16], nom[48];
        std::snprintf(cod, sizeof(cod), "C%08u", i);
        std::snprintf(nom, sizeof(nom), "ARTICULO NUMERO %06u", (kRows - i));
        AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)cod,
                     (UNSIGNED32)std::strlen(cod));
        AdsSetString(hTable, (UNSIGNED8*)"CNOMBRE", (UNSIGNED8*)nom,
                     (UNSIGNED32)std::strlen(nom));
        AdsSetString(hTable, (UNSIGNED8*)"CGRUPO", (UNSIGNED8*)"G01", 3);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "wide.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TCODIGO",
                             (UNSIGNED8*)"CCODIGO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TNOMBRE",
                             (UNSIGNED8*)"CNOMBRE", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TGRUPO",
                             (UNSIGNED8*)"CGRUPO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    SUBCASE("list_tags reports all three after reopen") {
        auto tags = openads::drivers::adi::AdiIndex::list_tags(
            (tmp / "wide.adi").string(), (tmp / "wide.adt").string());
        REQUIRE(tags);
        CHECK(tags.value().size() == 3u);
    }

    SUBCASE("the reopened table exposes all three orders and they navigate") {
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                             ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                             ADS_DEFAULT, &hT) == AE_SUCCESS);
        ADSHANDLE ah[8] = {0};
        UNSIGNED16 nOpen = 8;
        REQUIRE(AdsOpenIndex(hT, idxfile, ah, &nOpen) == AE_SUCCESS);
        CHECK(nOpen == 3);

        UNSIGNED16 nIdx = 0;
        REQUIRE(AdsGetNumIndexes(hT, &nIdx) == AE_SUCCESS);
        CHECK(nIdx == 3);

        // Every tag must be navigable, not merely counted. Activating each
        // one by HANDLE (tag ordinals depend on whether the bag prepends or
        // appends its directory entries, and tag-name identity is a separate
        // feature) and reading the first row in that order is enough: the
        // name tag orders by CNOMBRE, whose values count DOWN as recno grows,
        // so its first row must be the last one appended.
        std::vector<std::uint32_t> firsts;
        for (int i = 0; i < 3; ++i) {
            REQUIRE(AdsSetIndexOrderByHandle(hT, ah[i]) == AE_SUCCESS);
            REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
            UNSIGNED32 rn = 0;
            REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &rn) == AE_SUCCESS);
            firsts.push_back(rn);
        }
        // The CNOMBRE order is the one the old code silently dropped.
        CHECK(std::find(firsts.begin(), firsts.end(), (std::uint32_t)kRows)
              != firsts.end());

        AdsCloseTable(hT);
    }

    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
