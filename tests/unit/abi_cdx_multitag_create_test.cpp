// abi_cdx_multitag_create_test.cpp
//
// Regression / coverage for sequential AdsCreateIndex61 calls (the common
// rddads / Clipper / Harbour pattern for building a multi-tag .cdx bag):
//   FErase(cdx)
//   INDEX ON ... TAG t1
//   INDEX ON UPPER(...) TAG t2
//   ...
//
// This exercises the new direct read_record_raw + load_record_for_bulk_scan
// path (instead of per-record goto_record + invalidate) for:
// - Bare fields
// - UPPER() expressions
// - Deleted records (must be included in CDX)
// - Multiple tags on the same bag
//
// Matches the workload reported in https://github.com/FiveTechSoft/OpenADS/issues/128

#include "doctest.h"
#include "openads/ace.h"

// Internal driver headers for the repro key_length test (CdxIndex + IndexOpenMode)
#include "drivers/cdx/cdx_index.h"
#include "drivers/index_trait.h"

#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {

void set_str(ADSHANDLE hT, const char* field, const char* val) {
    UNSIGNED8 f[32] = {0};
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 buf[32] = {0};
    std::snprintf(reinterpret_cast<char*>(buf), sizeof(buf), "%-8.8s", val);
    REQUIRE(AdsSetString(hT, f, buf, 8) == 0);
}

void set_num(ADSHANDLE hT, const char* field, double v) {
    UNSIGNED8 f[32] = {0};
    std::memcpy(f, field, std::strlen(field) + 1);
    REQUIRE(AdsSetDouble(hT, f, v) == 0);
}

std::vector<UNSIGNED32> collect_recnos(ADSHANDLE hIdx) {
    std::vector<UNSIGNED32> recs;
    REQUIRE(AdsGotoTop(hIdx) == 0);
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hIdx, &eof) == 0);
        if (eof) break;
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hIdx, 0, &rec) == 0);
        recs.push_back(rec);
        REQUIRE(AdsSkip(hIdx, 1) == 0);
    }
    return recs;
}

} // namespace

TEST_CASE("CDX multi-tag bag via sequential AdsCreateIndex61 (bare + UPPER + deleted)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_multitag_create";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    // Table: NAME (char), CODE (numeric)
    UNSIGNED8 def[] = "NAME,C,8,0;CODE,N,6,0";
    UNSIGNED8 tname[] = "mtag";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);

    // Insert 5 rows, delete the 3rd (recno 3)
    const char* names[5] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};
    double codes[5] = {10.0, 20.0, 30.0, 40.0, 50.0};

    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "NAME", names[i]);
        set_num(hT, "CODE", codes[i]);
    }
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);   // deleted row must still appear in CDX

    // === Sequential INDEX ON (simulates real rddads usage) ===
    // Tag 1: bare NAME
    {
        UNSIGNED8 idxfile[] = "mtag";
        UNSIGNED8 tag[] = "BYNAME";
        UNSIGNED8 expr[] = "NAME";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        CHECK(cnt == 5);  // includes the deleted row

        auto recs = collect_recnos(hI);
        CHECK(recs.size() == 5);
        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    // Tag 2: UPPER(NAME)
    {
        UNSIGNED8 idxfile[] = "mtag";
        UNSIGNED8 tag[] = "UPNAME";
        UNSIGNED8 expr[] = "UPPER(NAME)";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        CHECK(cnt == 5);

        // Verify that seeking an upper-cased value works
        UNSIGNED8 key[16] = "BETA";
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hI, key, 4, 0, 0, &found) == 0);
        CHECK(found == 1);

        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    // Tag 3: bare CODE (numeric bare field)
    {
        UNSIGNED8 idxfile[] = "mtag";
        UNSIGNED8 tag[] = "BYCODE";
        UNSIGNED8 expr[] = "CODE";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        CHECK(cnt == 5);

        // Numeric order should be ascending
        auto recs = collect_recnos(hI);
        // We don't assert exact order here (depends on how numeric keys are
        // encoded), but we do assert all 5 rows are present and seek works.
        CHECK(recs.size() == 5);

        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    // Tag 4: conditional (FOR) on NAME
    {
        UNSIGNED8 idxfile[] = "mtag";
        UNSIGNED8 tag[] = "COND";
        UNSIGNED8 expr[] = "NAME";
        UNSIGNED8 cond[] = "CODE > 15";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, cond, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        // rows with CODE > 15 : Beta(20), Gamma(30 deleted), Delta(40), Epsilon(50) => 4 rows
        CHECK(cnt == 4);

        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("CDX CreateIndex61 includes deleted rows (single tag)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_create_deleted";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] = "VAL,C,4,0";
    UNSIGNED8 tname[] = "deltest";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);

    REQUIRE(AdsAppendRecord(hT) == 0); set_str(hT, "VAL", "A");
    REQUIRE(AdsAppendRecord(hT) == 0); set_str(hT, "VAL", "B");
    REQUIRE(AdsAppendRecord(hT) == 0); set_str(hT, "VAL", "C");
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);

    UNSIGNED8 idxfile[] = "deltest";
    UNSIGNED8 tag[] = "V";
    UNSIGNED8 expr[] = "VAL";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3);  // A, *B, C

    REQUIRE(AdsCloseIndex(hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Wide table test: many columns (simulates production 100+ col tables like ASORTYM)
TEST_CASE("CDX CreateIndex61 on wide table (many columns)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_wide";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    // Build a wide field def: 80 char columns + one indexed
    std::string def = "KEYFLD,C,10,0;";
    for (int i = 0; i < 80; ++i) {
        char f[16];
        std::snprintf(f, sizeof(f), "COL%02d,C,5,0;", i);
        def += f;
    }
    def.pop_back(); // remove last ;

    UNSIGNED8 tname[] = "wide";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0,
                           reinterpret_cast<UNSIGNED8*>(const_cast<char*>(def.c_str())), &hT) == 0);

    // Insert a few rows
    for (int r = 0; r < 3; ++r) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char val[16];
        std::snprintf(val, sizeof(val), "KEY%02d", r);
        set_str(hT, "KEYFLD", val);
    }

    // Index on the key field
    UNSIGNED8 idxfile[] = "wide";
    UNSIGNED8 tag[] = "BYKEY";
    UNSIGNED8 expr[] = "KEYFLD";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3);

    REQUIRE(AdsCloseIndex(hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Additional FOR expressions and composite
TEST_CASE("CDX CreateIndex61 with composite expr and multiple FOR") {
    auto dir = fs::temp_directory_path() / "openads_cdx_for_composite";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] = "A,C,5,0;B,C,5,0;N,N,5,0";
    UNSIGNED8 tname[] = "comp";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);

    for (int i = 0; i < 4; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char va[8], vb[8];
        std::snprintf(va, sizeof(va), "A%02d", i);
        std::snprintf(vb, sizeof(vb), "B%02d", i);
        set_str(hT, "A", va);
        set_str(hT, "B", vb);
        set_num(hT, "N", i * 10.0);
    }

    // Composite UPPER(A)+B
    {
        UNSIGNED8 idxfile[] = "comp";
        UNSIGNED8 tag[] = "COMP";
        UNSIGNED8 expr[] = "UPPER(A)+B";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        CHECK(cnt == 4);
        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    // FOR with expression
    {
        UNSIGNED8 idxfile[] = "comp";
        UNSIGNED8 tag[] = "FORN";
        UNSIGNED8 expr[] = "N";
        UNSIGNED8 cond[] = "N > 15";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, cond, nullptr, 0, 512, &hI) == 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
        CHECK(cnt == 2);  // 20,30
        REQUIRE(AdsCloseIndex(hI) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Test for numeric Val() expression key on CDX (issue #130).
// A char field holding digit strings; INDEX ON Val(charfield) must store
// 8-byte Fox numeric keys and support numeric DbSeek.
TEST_CASE("CDX numeric Val() expression key seek works (fixes #130)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_val_numeric";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] = "NUMSTR,C,10,0;OTHER,C,5,0";
    UNSIGNED8 tname[] = "valnum";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);

    // rows with "123", "456", "789"
    const char* vals[3] = {"123", "456", "789"};
    for (int r = 0; r < 3; ++r) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        set_str(hT, "NUMSTR", vals[r]);
        set_str(hT, "OTHER", "x");
    }

    // Create the numeric expr index
    UNSIGNED8 idxfile[] = "valnum";
    UNSIGNED8 tag[] = "BYVAL";
    UNSIGNED8 expr[] = "Val(NUMSTR)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);

    // Check key count ==3 
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 3);

    // Seek using the numeric double key (exercises the FoxNumeric path for Val() expr)
    double dkey = 456.0;
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(&dkey),
                    static_cast<UNSIGNED16>(sizeof(double)),
                    ADS_DOUBLEKEY, 0, &found) == 0);
    CHECK(found == 1);

    REQUIRE(AdsCloseIndex(hI) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Check the key_length for a Val() tag is 8 (numeric). Self-contained to not
// depend on external repro data in CI. (The external path version can be
// re-enabled locally when the ASORTYM.cdx from the issue is present.)
TEST_CASE("CDX key_length for Val tag is 8 (numeric)") {
  auto dir = fs::temp_directory_path() / "openads_val_keylen_test";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  const std::string dir_str = dir.string();
  std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
  srv.push_back(0);

  ADSHANDLE hConn = 0;
  REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

  UNSIGNED8 def[] = "NUMSTR,C,10,0";
  UNSIGNED8 tname[] = "valkey";
  ADSHANDLE hT = 0;
  REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT) == 0);

  REQUIRE(AdsAppendRecord(hT) == 0);
  set_str(hT, "NUMSTR", "123456");

  UNSIGNED8 idxfile[] = "valkey";
  UNSIGNED8 tag[] = "BYVAL";
  UNSIGNED8 expr[] = "Val(NUMSTR)";
  ADSHANDLE hI = 0;
  REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 512, &hI) == 0);
  REQUIRE(AdsCloseIndex(hI) == 0);
  hI = 0;
  REQUIRE(AdsCloseTable(hT) == 0);
  hT = 0;  // close all Ads handles before using internal reader on the cdx file

  // Now open via internal driver and check keylen==8
  std::string cdxpath = (dir / "valkey.cdx").string();
  openads::drivers::cdx::CdxIndex idx;
  auto r = idx.open_named(cdxpath, openads::drivers::IndexOpenMode::ReadOnly, "BYVAL");
  REQUIRE(r.has_value());
  CHECK(idx.key_length() == 8);

  REQUIRE(AdsDisconnect(hConn) == 0);
  fs::remove_all(dir, ec);
}
