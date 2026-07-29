// A decimal-less DBF numeric wider than int32 must survive the trip to ADT.
//
// Regression: adt_spec_for() mapped every N(n,0) to ADS_INTEGER, a 4-byte
// int32 capped at 2,147,483,647. A DBF N(19,0) holding 5,000,000,000 came back
// as 0 -- not rounded, zero, and nothing anywhere signalled the loss. Real
// schemas hit this: a credit limit N(16,0) in pesos, or the N(19,0) money
// columns of a tax-reporting table, outgrow int32 without being unusual.
//
// Narrow fields keep using INTEGER: 9 digits top out at 999,999,999, which
// always fits, and the 4-byte column stays half the size of a DOUBLE.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE wn_connect(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    const std::string s = dir.string();
    std::memcpy(srv, s.c_str(), s.size());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h)
            == AE_SUCCESS);
    return h;
}

UNSIGNED16 field_type_of(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[64]{};
    std::memcpy(fld, name, std::strlen(name));
    UNSIGNED16 t = 0;
    REQUIRE(AdsGetFieldType(hTable, fld, &t) == AE_SUCCESS);
    return t;
}

void set_num(ADSHANDLE hTable, const char* name, double v) {
    UNSIGNED8 fld[64]{};
    std::memcpy(fld, name, std::strlen(name));
    REQUIRE(AdsSetDouble(hTable, fld, v) == AE_SUCCESS);
}

double get_num(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[64]{};
    std::memcpy(fld, name, std::strlen(name));
    double v = 0;
    REQUIRE(AdsGetDouble(hTable, fld, &v) == AE_SUCCESS);
    return v;
}

}  // namespace

TEST_CASE("ADT: decimal-less numerics wider than int32 round-trip intact") {
    fs::path tmp = fs::temp_directory_path() / "openads_adt_wide_numeric";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    ADSHANDLE hConn = wn_connect(tmp);

    UNSIGNED8 tbl[]    = "wide.adt";
    // NARROW must stay INTEGER; the rest must widen to DOUBLE.
    UNSIGNED8 flddef[] = "NARROW,Numeric,9;CUPO,Numeric,16;MONTO,Numeric,19;MED,Numeric,12";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    CHECK(field_type_of(hTable, "NARROW") == ADS_INTEGER);
    CHECK(field_type_of(hTable, "CUPO")   == ADS_DOUBLE);
    CHECK(field_type_of(hTable, "MONTO")  == ADS_DOUBLE);
    CHECK(field_type_of(hTable, "MED")    == ADS_DOUBLE);

    // Values straddling the int32 ceiling (2,147,483,647).
    const double narrow = 999999999.0;          // largest 9-digit value
    const double cupo   = 5000000000.0;         // 5e9  -> was 0 before
    const double monto  = 999999999999999.0;    // ~1e15, still exact in a double
    const double med    = 2147483648.0;         // ceiling + 1

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    set_num(hTable, "NARROW", narrow);
    set_num(hTable, "CUPO",   cupo);
    set_num(hTable, "MONTO",  monto);
    set_num(hTable, "MED",    med);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Reopen so the values come off disk, not out of the write buffer.
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, tbl, ADS_ADT, ADS_ANSI, 0, 0, 1, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);

    CHECK(get_num(hTable, "NARROW") == doctest::Approx(narrow));
    CHECK(get_num(hTable, "CUPO")   == doctest::Approx(cupo));
    CHECK(get_num(hTable, "MONTO")  == doctest::Approx(monto));
    CHECK(get_num(hTable, "MED")    == doctest::Approx(med));

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    AdsDisconnect(hConn);
}
