#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Issue #127 — NTXPL852 / PL852 OEM collation (see also
// abi_ntxpl852_collation_abi_test.cpp and oem_collation_test.cpp).

TEST_CASE("NTXPL852 soft seek positions M after L-stroke block") {
    auto dir = fs::temp_directory_path() / "openads_ntxpl852_seek";
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
    UNSIGNED8 tname[] = "names";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct Row { const char* name; };
    const Row rows[] = {
        {"LACAAAAA"},
        { "\x9D\x41BBBBBB" },  // ŁAB...
        {"MADCCCCC"},
        {"ZBYDDDDD"},
    };
    UNSIGNED8 fName[] = "NAME";
    for (const auto& row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row.name)),
            static_cast<UNSIGNED32>(std::strlen(row.name))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "names.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == 0);

    const char seek_ch = 'M';
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(const_cast<char*>(&seek_ch)),
                    1u, ADS_STRING, /*soft=*/1, &found) == 0);
    CHECK(found == 1);  // single-char prefix matches first byte exactly

    UNSIGNED8 got[32];
    UNSIGNED32 got_len = sizeof(got);
    REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
    std::string name(reinterpret_cast<char*>(got), got_len);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    CHECK(name.substr(0, 3) == "MAD");

    // Walk index order: LAC, ŁAB, MAD, ZBY.
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED32 rec = 0;
    const unsigned char expect_first[] = { 'L', 0x9D, 'M', 'Z' };
    for (unsigned char ef : expect_first) {
        REQUIRE(AdsGetRecordNum(hT, 0, &rec) == 0);
        got_len = sizeof(got);
        REQUIRE(AdsGetString(hT, fName, got, &got_len, 0) == 0);
        CHECK(static_cast<unsigned char>(got[0]) == ef);
        if (ef != 'Z') REQUIRE(AdsSkip(hT, 1) == 0);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("NTXPL852 hard seek requires exact key match") {
    auto dir = fs::temp_directory_path() / "openads_ntxpl852_hard";
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
    UNSIGNED8 tname[] = "names";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    const char* rows[] = {"LACAAAAA", "\x9D\x41BBBBBB", "MADCCCCC"};
    UNSIGNED8 fName[] = "NAME";
    for (const char* row : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, fName,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(row)),
            static_cast<UNSIGNED32>(std::strlen(row))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    UNSIGNED8 bag[]  = "names.cdx";
    UNSIGNED8 tag[]  = "BYNAME";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    std::string exact = "MADCCCCC";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(exact.data()),
                    static_cast<UNSIGNED16>(exact.size()),
                    ADS_STRING, /*soft=*/0, &found) == 0);
    CHECK(found == 1);

    std::string absent = "MXZZZZZZ";
    found = 99;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(absent.data()),
                    static_cast<UNSIGNED16>(absent.size()),
                    ADS_STRING, /*soft=*/0, &found) == 0);
    CHECK(found == 0);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}