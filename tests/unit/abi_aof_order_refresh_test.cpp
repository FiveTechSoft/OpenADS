// abi_aof_order_refresh_test.cpp -- with an AOF and a controlling index
// order active, a key edit that keeps AOF membership must re-sort the
// filtered walk: the materialized sequence cache may not outlive the
// index content it was sorted from. Covers a same-handle commit and a
// peer handle's commit observed through refresh_from_disk. No manual
// AdsRefreshAOF is required for ORDER; that API re-evaluates MEMBERSHIP.
//
// Table: 3 rows CODE = A,B,C (recnos 1,2,3). Edit recno 1 key A->Z.
// Correct index order after the edit is B,C,Z -> recnos 2,3,1.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

UNSIGNED8 fCodeOrd[] = "CODE";
UNSIGNED8 tagOrd[]   = "CODE";
UNSIGNED8 fltOrd[]   = "CODE >= 'A'";

std::string walk_ord(ADSHANDLE hT) {
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

void make_ord_table(const fs::path& dir, ADSHANDLE& hConn) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    ADSHANDLE hT = 0;
    UNSIGNED8 def[]   = "CODE,C,4,0";
    UNSIGNED8 tname[] = "ordrf.dbf";
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == 0);
    const char* keys[3] = {"A   ", "B   ", "C   "};
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fCodeOrd, (UNSIGNED8*)keys[i], 4) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    UNSIGNED8 bag[]  = "ordrf.cdx";
    UNSIGNED8 expr[] = "CODE";
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex(hT, bag, tagOrd, expr, nullptr, 0, 0, &hIx) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
}

ADSHANDLE open_ord_shared(ADSHANDLE hConn) {
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "ordrf.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, 0, 0,
                         ADS_SHARED, &hT) == 0);
    return hT;
}

void edit_key_A_to_Z(ADSHANDLE hT) {
    REQUIRE(AdsGotoRecord(hT, 1) == 0);
    REQUIRE(AdsLockRecord(hT, 1) == 0);
    REQUIRE(AdsSetString(hT, fCodeOrd, (UNSIGNED8*)"Z   ", 4) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsUnlockRecord(hT, 1) == 0);
}

} // namespace

TEST_CASE("AOF: same-handle key edit re-sorts the filtered walk") {
    auto dir = fs::temp_directory_path() / "openads_aof_ordrf_same";
    ADSHANDLE hConn = 0;
    make_ord_table(dir, hConn);
    ADSHANDLE hT = open_ord_shared(hConn);

    REQUIRE(AdsSetAOF(hT, fltOrd, 0) == 0);
    REQUIRE(AdsSetIndexOrder(hT, tagOrd) == 0);
    CHECK(walk_ord(hT) == "1,2,3");   // A,B,C

    edit_key_A_to_Z(hT);

    // No AdsRefreshAOF: navigation must follow the NEW index order B,C,Z.
    CHECK(walk_ord(hT) == "2,3,1");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("AOF: descending order re-sorts after key edit, both directions") {
    // Descending tag: after A->Z the descending order is Z,C,B = 1,3,2.
    auto dir = fs::temp_directory_path() / "openads_aof_ordrf_desc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = 0, hT = 0;
    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    UNSIGNED8 def[]   = "CODE,C,4,0";
    UNSIGNED8 tname[] = "ordrf.dbf";
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == 0);
    const char* keys[3] = {"A   ", "B   ", "C   "};
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fCodeOrd, (UNSIGNED8*)keys[i], 4) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    UNSIGNED8 bag[]  = "ordrf.cdx";
    UNSIGNED8 dtag[] = "DCODE";
    UNSIGNED8 expr[] = "CODE";
    ADSHANDLE hIx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, dtag, expr, nullptr, nullptr,
                             ADS_COMPOUND | ADS_DESCENDING, 512, &hIx) == 0);
    REQUIRE(AdsSetAOF(hT, fltOrd, 0) == 0);
    REQUIRE(AdsSetIndexOrder(hT, dtag) == 0);

    // Descending before the edit: C,B,A = 3,2,1.
    CHECK(walk_ord(hT) == "3,2,1");

    edit_key_A_to_Z(hT);

    // After A->Z: Z,C,B = 1,3,2 top-down; B,C,Z = 2,3,1 bottom-up.
    CHECK(walk_ord(hT) == "1,3,2");
    std::string back;
    REQUIRE(AdsGotoBottom(hT) == 0);
    for (;;) {
        UNSIGNED16 bof = 0;
        REQUIRE(AdsAtBOF(hT, &bof) == 0);
        if (bof) break;
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hT, ADS_IGNOREFILTERS, &r) == 0);
        if (!back.empty()) back += ',';
        back += std::to_string(r);
        REQUIRE(AdsSkip(hT, -1) == 0);
    }
    CHECK(back == "2,3,1");

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AOF: peer-handle key edit re-sorts the filtered walk") {
    auto dir = fs::temp_directory_path() / "openads_aof_ordrf_peer";
    ADSHANDLE hConn = 0;
    make_ord_table(dir, hConn);
    ADSHANDLE hT1 = open_ord_shared(hConn);
    ADSHANDLE hT2 = open_ord_shared(hConn);

    REQUIRE(AdsSetAOF(hT1, fltOrd, 0) == 0);
    REQUIRE(AdsSetIndexOrder(hT1, tagOrd) == 0);
    CHECK(walk_ord(hT1) == "1,2,3");

    edit_key_A_to_Z(hT2);

    // hT1's GoTop refreshes the bag from disk and must see B,C,Z.
    CHECK(walk_ord(hT1) == "2,3,1");

    AdsCloseTable(hT1);
    AdsCloseTable(hT2);
    AdsDisconnect(hConn);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("AOF: peer-connection key edit re-sorts the filtered walk") {
    // Stronger than the two-handle case: the edit arrives through a
    // SEPARATE connection, so the observing handle's index can only learn
    // of it via the on-disk bag (reload_header_if_changed_).
    auto dir = fs::temp_directory_path() / "openads_aof_ordrf_peerconn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    ADSHANDLE hConn1 = 0;
    make_ord_table(dir, hConn1);
    ADSHANDLE hT1 = open_ord_shared(hConn1);
    REQUIRE(AdsSetAOF(hT1, fltOrd, 0) == 0);
    REQUIRE(AdsSetIndexOrder(hT1, tagOrd) == 0);
    CHECK(walk_ord(hT1) == "1,2,3");

    ADSHANDLE hConn2 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn2) == 0);
    ADSHANDLE hT2 = 0;
    UNSIGNED8 tname[] = "ordrf.dbf";
    REQUIRE(AdsOpenTable(hConn2, tname, nullptr, ADS_CDX, ADS_ANSI, 0, 0,
                         ADS_SHARED, &hT2) == 0);
    edit_key_A_to_Z(hT2);

    // hT1's GoTop refreshes the bag from disk and must see B,C,Z.
    CHECK(walk_ord(hT1) == "2,3,1");

    AdsCloseTable(hT1);
    AdsCloseTable(hT2);
    AdsDisconnect(hConn1);
    AdsDisconnect(hConn2);
    fs::remove_all(dir, ec);
}
