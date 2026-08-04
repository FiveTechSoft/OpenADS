// SAP date display formatting — every expectation here was probed against
// ace64.dll (see docs/date-display-format.md). The core split:
//
//   AdsGetFIELD  -> formatted per the date format ("01/15/2024")
//   AdsGetSTRING -> raw storage text ("20240115"), format-independent
//
// That asymmetry is SAP's own behaviour. Do not "unify" the two: the S4
// parity harness reads through GetField (formatted), while php_ads and the
// engine internals read raw. Deviations from SAP (GetString on timestamps,
// string writes into date fields) are deliberate and documented — do not
// adjust the expectations toward SAP without reading the doc.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string get_field(ADSHANDLE h, const char* fld) {
    UNSIGNED8 f[32] = {0};
    std::strncpy(reinterpret_cast<char*>(f), fld, sizeof(f) - 1);
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 len = 40;
    if (AdsGetField(h, f, buf, &len, ADS_NONE) != 0) return "<err>";
    return std::string(reinterpret_cast<char*>(buf),
                       static_cast<std::size_t>(len));
}

std::string get_string(ADSHANDLE h, const char* fld) {
    UNSIGNED8 f[32] = {0};
    std::strncpy(reinterpret_cast<char*>(f), fld, sizeof(f) - 1);
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 len = 40;
    if (AdsGetString(h, f, buf, &len, ADS_NONE) != 0) return "<err>";
    return std::string(reinterpret_cast<char*>(buf),
                       static_cast<std::size_t>(len));
}

struct FormatRestore {
    ~FormatRestore() {
        UNSIGNED8 def[] = "MM/DD/CCYY";
        AdsSetDateFormat(def);
    }
};

} // namespace

TEST_CASE("SAP date display: GetField formats, GetString stays raw") {
    FormatRestore restore_format;
    auto dir = fs::temp_directory_path() / "openads_date_fmt";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    UNSIGNED8 srv[260] = {0};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    SUBCASE("the default format is SAP's MM/DD/CCYY") {
        UNSIGNED8 f[40] = {0};
        UNSIGNED16 fl = sizeof(f);
        REQUIRE(AdsGetDateFormat(f, &fl) == 0);
        CHECK(std::string(reinterpret_cast<char*>(f)) == "MM/DD/CCYY");
    }

    UNSIGNED8 tbl[] = "datefmt.adt";
    UNSIGNED8 defs[] = "Dt,Date,8;Ts,TimeStamp,8;Nm,Character,6";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           defs, &hT) == 0);

    // row 1: a real date (2024-01-15 = JDN 2460325)
    REQUIRE(AdsAppendRecord(hT) == 0);
    { UNSIGNED8 f[] = "Dt"; REQUIRE(AdsSetJulian(hT, f, 2460325) == 0); }
    REQUIRE(AdsWriteRecord(hT) == 0);
    // row 2: a blank date via AdsSetEmpty
    REQUIRE(AdsAppendRecord(hT) == 0);
    { UNSIGNED8 f[] = "Dt"; REQUIRE(AdsSetEmpty(hT, f) == 0); }
    REQUIRE(AdsWriteRecord(hT) == 0);
    // row 3: a timestamp written the SAP way (24-hour input text)
    REQUIRE(AdsAppendRecord(hT) == 0);
    { UNSIGNED8 f[] = "Ts";
      UNSIGNED8 v[] = "01/15/2024 13:45:59";
      REQUIRE(AdsSetTimeStamp(hT, f, v, 19) == 0); }
    REQUIRE(AdsWriteRecord(hT) == 0);
    // row 4: a date written as text in the current format
    REQUIRE(AdsAppendRecord(hT) == 0);
    { UNSIGNED8 f[] = "Dt";
      UNSIGNED8 v[] = "03/07/2025";
      REQUIRE(AdsSetDate(hT, f, v, 10) == 0); }
    REQUIRE(AdsWriteRecord(hT) == 0);

    REQUIRE(AdsGotoTop(hT) == 0);

    SUBCASE("real date: GetField formatted, GetString raw, both engines' rule") {
        CHECK(get_field(hT, "Dt")  == "01/15/2024");
        CHECK(get_string(hT, "Dt") == "20240115");
    }

    SUBCASE("blank date: GetField '  /  /    ', GetString 8 spaces") {
        REQUIRE(AdsSkip(hT, 1) == 0);
        CHECK(get_field(hT, "Dt")  == "  /  /    ");
        CHECK(get_string(hT, "Dt") == "        ");
    }

    SUBCASE("timestamp: 12-hour clock with 2-digit hour, blank date part blanked") {
        REQUIRE(AdsSkip(hT, 2) == 0);
        CHECK(get_field(hT, "Ts") == "01/15/2024 01:45:59 PM");
        // Deviation from SAP (which raises 5066): raw compact form for php.
        CHECK(get_string(hT, "Ts") == "20240115134559");
        // Ts is blank on row 1:
        REQUIRE(AdsGotoTop(hT) == 0);
        CHECK(get_field(hT, "Ts") == "  /  /     12:00:00 AM");
    }

    SUBCASE("AdsSetDate parsed the current-format text") {
        REQUIRE(AdsSkip(hT, 3) == 0);
        CHECK(get_string(hT, "Dt") == "20250307");
        UNSIGNED8 f[] = "Dt";
        SIGNED32 j = 0;
        REQUIRE(AdsGetJulian(hT, f, &j) == 0);
        CHECK(j == 2460742);
    }

    SUBCASE("AdsSetDateFormat: normalised to CCYY and drives GetField + AdsGetDate") {
        UNSIGNED8 fmt[] = "DD.MM.YYYY";
        REQUIRE(AdsSetDateFormat(fmt) == 0);
        UNSIGNED8 f[40] = {0};
        UNSIGNED16 fl = sizeof(f);
        REQUIRE(AdsGetDateFormat(f, &fl) == 0);
        CHECK(std::string(reinterpret_cast<char*>(f)) == "DD.MM.CCYY");
        CHECK(get_field(hT, "Dt") == "15.01.2024");
        CHECK(get_string(hT, "Dt") == "20240115");   // raw is format-immune
        {
            UNSIGNED8 fd[] = "Dt";
            UNSIGNED8 buf[40] = {0};
            UNSIGNED16 blen = sizeof(buf);
            REQUIRE(AdsGetDate(hT, fd, buf, &blen) == 0);
            CHECK(std::string(reinterpret_cast<char*>(buf)) == "15.01.2024");
        }
        // Writing under the custom format parses per that format.
        REQUIRE(AdsAppendRecord(hT) == 0);
        {
            UNSIGNED8 fd[] = "Dt";
            UNSIGNED8 v[] = "25.12.2023";
            REQUIRE(AdsSetDate(hT, fd, v, 10) == 0);
            SIGNED32 j = 0;
            REQUIRE(AdsGetJulian(hT, fd, &j) == 0);
            CHECK(j == 2460304);   // 2023-12-25
        }
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
