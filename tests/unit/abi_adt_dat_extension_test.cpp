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

// TODO.parity.md backlog item 3 claimed AdsSetString into an ADT DOUBLE
// stores the text verbatim ("10.50" read back as ~6.01e-154). The repro:
// an ADT table declared Numeric,12,2 — which adt_spec_for maps to a
// binary DOUBLE — written via AdsSetString. The remote-twin work later
// taught encode_field_string to parse strings into every ADT/VFP binary
// numeric type, which fixed this path too; this test pins the repro so
// the claim stays verified (and the encode never regresses to memcpy).
TEST_CASE("AdsSetString into an ADT DOUBLE parses the text (was verbatim)") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_setstr_double";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    UNSIGNED8 srv[260] = {0};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl[]  = "dblprobe.adt";
    UNSIGNED8 defs[] = "V,Numeric,12,2;W,Integer,4";   // V -> ADT DOUBLE
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI,
                           0, 0, 0, defs, &hT) == 0);

    REQUIRE(AdsAppendRecord(hT) == 0);
    {
        UNSIGNED8 f[] = "V";
        UNSIGNED8 v[] = "10.50";
        REQUIRE(AdsSetString(hT, f, v, 5) == 0);
    }
    {
        UNSIGNED8 f[] = "W";
        UNSIGNED8 v[] = "42";
        REQUIRE(AdsSetString(hT, f, v, 2) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);

    {   // binary read: the VALUE round-trips, not the bytes of the text
        UNSIGNED8 f[] = "V";
        double d = 0.0;
        REQUIRE(AdsGetDouble(hT, f, &d) == 0);
        CHECK(d == doctest::Approx(10.50));
    }
    {
        UNSIGNED8 f[] = "W";
        double d = 0.0;
        REQUIRE(AdsGetDouble(hT, f, &d) == 0);
        CHECK(d == doctest::Approx(42.0));
    }
    {   // string read renders the number, not verbatim garbage
        UNSIGNED8 f[] = "V";
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hT, f, buf, &len, ADS_NONE) == 0);
        std::string s(reinterpret_cast<char*>(buf), len);
        CHECK(s.find("10.5") != std::string::npos);
        CHECK(s.find("e-") == std::string::npos);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
