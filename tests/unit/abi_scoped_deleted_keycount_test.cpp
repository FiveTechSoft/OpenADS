// Scoped OrdKeyCount with SET DELETED ON must exclude deleted rows.
// Remote xBrowse otherwise paints ghost rows and may hang on close.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("local scoped key count excludes deleted (SET DELETED ON)") {
    auto dir = fs::temp_directory_path() / "oads_scdel_loc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    REQUIRE(fs::is_directory(dir));

    std::string path = dir.string();
    std::vector<UNSIGNED8> srv(path.begin(), path.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 def[] = "CODE,C,8,0;NAME,C,10,0";
    UNSIGNED8 tname[] = "parts";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                           def, &hT) == 0);

    auto put = [&](const char* code, const char* name) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 f1[] = "CODE";
        UNSIGNED8 f2[] = "NAME";
        AdsSetString(hT, f1, (UNSIGNED8*)code, (UNSIGNED32)std::strlen(code));
        AdsSetString(hT, f2, (UNSIGNED8*)name, (UNSIGNED32)std::strlen(name));
        REQUIRE(AdsWriteRecord(hT) == 0);
    };
    put("AA", "live-1");
    put("BB", "deleted");
    put("CC", "live-2");

    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED8 bag[] = "parts.cdx";
    UNSIGNED8 tag[] = "BYCODE";
    UNSIGNED8 expr[] = "CODE";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr, 0, 0,
                             &hIdx) == 0);
    REQUIRE(AdsSetIndexOrder(hT, tag) == 0);

    AdsShowDeleted(0);
    REQUIRE(AdsSetScope(hIdx, ADS_TOP, (UNSIGNED8*)"AA", 2, ADS_STRINGKEY) ==
            0);
    REQUIRE(AdsSetScope(hIdx, ADS_BOTTOM, (UNSIGNED8*)"CC", 2,
                        ADS_STRINGKEY) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    int nvis = 0;
    for (int g = 0; g < 10; ++g) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hT, &eof);
        if (eof) break;
        ++nvis;
        REQUIRE(AdsSkip(hT, 1) == 0);
    }
    CHECK(nvis == 2);

    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == 0);
    CHECK(kc == 2);

    kc = 0;
    REQUIRE(AdsGetRecordCount(hIdx, 0, &kc) == 0);
    CHECK(kc == 2);

    AdsShowDeleted(1);
    kc = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == 0);
    CHECK(kc == 3);

    AdsShowDeleted(0);
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("remote scoped key count excludes deleted (SET DELETED ON)") {
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "oads_scdel_rem";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);

    // Stage locally under data dir
    {
        std::string path = data.string();
        std::vector<UNSIGNED8> srv(path.begin(), path.end());
        srv.push_back(0);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        UNSIGNED8 def[] = "CODE,C,8,0;NAME,C,10,0";
        UNSIGNED8 tname[] = "parts";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, 0, 0,
                               0, def, &hT) == 0);
        auto put = [&](const char* code, const char* name) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            UNSIGNED8 f1[] = "CODE";
            UNSIGNED8 f2[] = "NAME";
            AdsSetString(hT, f1, (UNSIGNED8*)code,
                         (UNSIGNED32)std::strlen(code));
            AdsSetString(hT, f2, (UNSIGNED8*)name,
                         (UNSIGNED32)std::strlen(name));
            REQUIRE(AdsWriteRecord(hT) == 0);
        };
        put("AA", "live-1");
        put("BB", "deleted");
        put("CC", "live-2");
        AdsShowDeleted(1);
        REQUIRE(AdsGotoRecord(hT, 2) == 0);
        REQUIRE(AdsDeleteRecord(hT) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
        UNSIGNED8 bag[] = "parts.cdx";
        UNSIGNED8 tag[] = "BYCODE";
        UNSIGNED8 expr[] = "CODE";
        ADSHANDLE hIdx = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr, 0, 0,
                                 &hIdx) == 0);
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    Server srv;
    srv.set_data_dir(data.string());
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" +
                      data.generic_string();
    std::vector<UNSIGNED8> ub(uri.begin(), uri.end());
    ub.push_back(0);
    ADSHANDLE hConn = 0;
    AdsShowDeleted(0);
    REQUIRE(AdsConnect60(ub.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    UNSIGNED8 tname[] = "parts";
    ADSHANDLE hRem = 0;
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, ADS_ANSI, 0, 0, 0,
                         &hRem) == 0);
    UNSIGNED8 bag[] = "parts.cdx";
    ADSHANDLE ahIdx[8] = {};
    UNSIGNED16 nIdx = 8;
    REQUIRE(AdsOpenIndex(hRem, bag, ahIdx, &nIdx) == 0);
    REQUIRE(nIdx >= 1);
    ADSHANDLE hOrd = ahIdx[0];
    UNSIGNED8 tag[] = "BYCODE";
    REQUIRE(AdsSetIndexOrder(hRem, tag) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, (UNSIGNED8*)"AA", 2, ADS_STRINGKEY) ==
            0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, (UNSIGNED8*)"CC", 2,
                        ADS_STRINGKEY) == 0);
    REQUIRE(AdsGotoTop(hRem) == 0);

    int nvis = 0;
    for (int g = 0; g < 10; ++g) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hRem, &eof);
        if (eof) break;
        ++nvis;
        REQUIRE(AdsSkip(hRem, 1) == 0);
    }
    CHECK(nvis == 2);

    UNSIGNED32 kc = 99;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == 0);
    CHECK(kc == 2);

    AdsCloseTable(hRem);
    AdsDisconnect(hConn);
    fs::remove_all(data, ec);
}
