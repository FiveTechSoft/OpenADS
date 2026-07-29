// An ADT table kept under a non-.adt extension must work end to end.
//
// `ExtFile='.DAT'` is a common ERP convention: every table is named .DAT
// whatever its format. Three places assumed the extension instead of the
// format, so such a table was created as <stem>.adt behind the caller's back,
// reopened under a name it never asked for, and given a .cdx structural bag
// that the ADT driver cannot use.
//
// #143 fixed the SQL side of the same convention by sniffing the header. This
// covers the navigational side.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("ADT: a table named .DAT creates, reopens and indexes as ADT") {
    fs::path tmp = fs::temp_directory_path() / "openads_adt_dat_ext";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "conseinv.dat";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10;CNOMBRE,Character,30";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // Created under the name the caller asked for, not silently as .adt.
    CHECK(fs::exists(tmp / "conseinv.dat"));
    CHECK_FALSE(fs::exists(tmp / "conseinv.adt"));

    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char cod[16];
        std::snprintf(cod, sizeof(cod), "C%04d", 10 - i);
        AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)cod,
                     (UNSIGNED32)std::strlen(cod));
        AdsSetString(hTable, (UNSIGNED8*)"CNOMBRE", (UNSIGNED8*)"X", 1);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    // Structural bag: no name given, so it must be the ADT companion .adi.
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, (UNSIGNED8*)"", (UNSIGNED8*)"TCODIGO",
                             (UNSIGNED8*)"CCODIGO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    CHECK(fs::exists(tmp / "conseinv.adi"));
    CHECK_FALSE(fs::exists(tmp / "conseinv.cdx"));

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    SUBCASE("reopen by the same name, with its index, and navigate in key order") {
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                             ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                             ADS_DEFAULT, &hT) == AE_SUCCESS);

        UNSIGNED32 rc = 0;
        REQUIRE(AdsGetRecordCount(hT, ADS_IGNOREFILTERS, &rc) == AE_SUCCESS);
        CHECK(rc == 10);

        // The .adi companion is found from the .DAT name.
        ADSHANDLE ah[4] = {0};
        UNSIGNED16 nOpen = 4;
        REQUIRE(AdsOpenIndex(hT, (UNSIGNED8*)"conseinv.adi", ah, &nOpen)
                == AE_SUCCESS);
        REQUIRE(nOpen >= 1);

        REQUIRE(AdsSetIndexOrderByHandle(hT, ah[0]) == AE_SUCCESS);
        REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
        UNSIGNED8  buf[32]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hT, (UNSIGNED8*)"CCODIGO", buf, &len, 0)
                == AE_SUCCESS);
        std::string first(reinterpret_cast<char*>(buf), len);
        while (!first.empty() && first.back() == ' ') first.pop_back();
        CHECK(first == "C0001");   // lowest key, appended last

        AdsCloseTable(hT);
    }

    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
