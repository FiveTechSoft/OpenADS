// abi_aof_index_order_test.cpp -- a filtered walk must follow the active
// index order (SAP ADS behaviour), not record order; and a non-optimisable
// AdsSetFilter must hide the rows it rejects (local table).
//
// Table: 8 rows, CODE is the reverse of recno so index order = reverse recno.
// GRP = "BW" on recnos 2, 4, 7.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

UNSIGNED8 fCode[] = "CODE";
UNSIGNED8 fGrp[]  = "GRP";

std::string walk(ADSHANDLE hT) {
    std::string out;
    REQUIRE(AdsGotoTop(hT) == 0);
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        if (!out.empty()) out += ',';
        out += std::to_string(r);
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    return out;
}

std::string back_walk(ADSHANDLE hT) {
    std::string out;
    REQUIRE(AdsGotoBottom(hT) == 0);
    for (;;) {
        UNSIGNED16 bof = 0;
        REQUIRE(AdsAtBOF(hT, &bof) == 0);
        if (bof) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        if (!out.empty()) out += ',';
        out += std::to_string(r);
        REQUIRE(AdsSkip(hT, -1) == 0);
    }
    return out;
}

void make_table(const fs::path& dir, ADSHANDLE& hConn, ADSHANDLE& hT) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    UNSIGNED8 def[]   = "CODE,C,4,0;GRP,C,2,0";
    UNSIGNED8 tname[] = "ord.dbf";
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == 0);
    for (int i = 1; i <= 8; ++i) {
        char code[8];
        std::snprintf(code, sizeof code, "C%03d", 9 - i);   // C008..C001
        const char* g = (i == 2 || i == 4 || i == 7) ? "BW" : "XX";
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, fCode, (UNSIGNED8*)code, 4);
        AdsSetString(hT, fGrp, (UNSIGNED8*)g, 2);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    UNSIGNED8 bag[]  = "ord.cdx";
    UNSIGNED8 tag[]  = "CODE";
    UNSIGNED8 expr[] = "CODE";
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex(hT, bag, tag, expr, nullptr, 0, 0, &hIx) == 0);
}

} // namespace

TEST_CASE("AOF: filtered walk follows the active index order") {
    auto dir = fs::temp_directory_path() / "openads_aof_index_order";
    ADSHANDLE hConn = 0, hT = 0;
    make_table(dir, hConn, hT);

    UNSIGNED8 flt[] = "GRP = 'BW'";
    // Natural order first: record order.
    REQUIRE(AdsSetAOF(hT, flt, 0) == 0);
    {
        UNSIGNED8 none[] = "";
        (void)AdsSetIndexOrder(hT, none);
    }
    CHECK(walk(hT) == "2,4,7");

    // Controlling index CODE (reverse of recno): index order is 7,4,2.
    UNSIGNED8 tag[] = "CODE";
    REQUIRE(AdsSetIndexOrder(hT, tag) == 0);
    CHECK(walk(hT) == "7,4,2");
    CHECK(back_walk(hT) == "2,4,7");

    // GoTo then Skip(-1) steps along the index order from that row.
    REQUIRE(AdsGotoRecord(hT, 4) == 0);
    REQUIRE(AdsSkip(hT, -1) == 0);
    UNSIGNED32 r = 0;
    REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
    CHECK(r == 7);

    // Clearing the filter restores the full ordered walk.
    REQUIRE(AdsClearAOF(hT) == 0);
    CHECK(walk(hT) == "8,7,6,5,4,3,2,1");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetFilter: non-optimisable expression hides rejected rows") {
    auto dir = fs::temp_directory_path() / "openads_setfilter_hides";
    ADSHANDLE hConn = 0, hT = 0;
    make_table(dir, hConn, hT);

    UNSIGNED8 flt[] = "SUBSTR(GRP,1,2) = 'BW'";
    {
        // AdsCreateIndex leaves the new tag as the controlling order.
        UNSIGNED8 none[] = "";
        (void)AdsSetIndexOrder(hT, none);
    }
    REQUIRE(AdsSetFilter(hT, flt) == 0);
    CHECK(walk(hT) == "2,4,7");

    UNSIGNED8 tag[] = "CODE";
    REQUIRE(AdsSetIndexOrder(hT, tag) == 0);
    CHECK(walk(hT) == "7,4,2");

    REQUIRE(AdsClearFilter(hT) == 0);
    CHECK(walk(hT) == "8,7,6,5,4,3,2,1");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
