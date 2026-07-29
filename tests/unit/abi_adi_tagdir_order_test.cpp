// Tag ORDINALS in an .ADI bag must follow creation order, as CDX does.
//
// add_tag() prepended its tag-directory entry, so the ordinals came out
// reversed: after creating TCODIGO, TNOMBRE, TGRUPO in that order, ordinal 1
// was TGRUPO. An application that navigates orders by NUMBER -- OrdSetFocus(n)
// / OrdName(n), which is what a browse doing click-to-sort on a column does --
// then activates the wrong order, and it cannot compensate without knowing in
// advance how many tags the bag will end up holding. The same application over
// a .CDX bag gets creation order.
//
// The existing wide-page test deliberately avoids asserting ordinals ("tag
// ordinals depend on whether the bag prepends or appends") -- this test pins
// them.

#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("ADI: tag ordinals follow creation order, like CDX") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_tagdir_order";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "ord.adt";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10;CNOMBRE,Character,40;CGRUPO,Character,6";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    for (std::uint32_t i = 0; i < 50u; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char cod[16];
        std::snprintf(cod, sizeof(cod), "C%08u", i);
        AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)cod,
                     (UNSIGNED32)std::strlen(cod));
        AdsSetString(hTable, (UNSIGNED8*)"CNOMBRE", (UNSIGNED8*)"N", 1);
        AdsSetString(hTable, (UNSIGNED8*)"CGRUPO", (UNSIGNED8*)"G01", 3);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "ord.adi";
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

    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (tmp / "ord.adi").string(), (tmp / "ord.adt").string());
    REQUIRE(tags);
    REQUIRE(tags.value().size() == 3u);

    // Directory order == creation order. Before the fix this read
    // {CGRUPO, CNOMBRE, CCODIGO}.
    CHECK(tags.value()[0] == "CCODIGO");
    CHECK(tags.value()[1] == "CNOMBRE");
    CHECK(tags.value()[2] == "CGRUPO");

    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
