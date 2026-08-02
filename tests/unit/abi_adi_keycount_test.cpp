// Regression test for GitHub issue #149:
//   "AdsGetKeyCount over an ADI order returns 1 on a 20-row table"
//
// Root cause: AdiIndex::ordered_recnos_cached() collected only entry 0 of
// each dense leaf (one push per leaf), so the cached walk held
// 1 + (#right siblings) entries. With 20 char keys in a single dense leaf
// the cache size was 1 and AdsGetKeyCount(hIdx) returned 1 instead of 20.
//
// Also covers the second half of the issue: the NTX/ADI branches of
// AdsGetKeyCount did not honour SET DELETED (AdsShowDeleted(0)); the ADI
// branch now filters deleted rows like the CDX branch.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

ADSHANDLE make_adi_table(const fs::path& dir, const char* tablename,
                         ADSHANDLE* phConn) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, phConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[64]{};
    std::strncpy(reinterpret_cast<char*>(tbl), tablename, sizeof(tbl) - 1);
    UNSIGNED8 flddef[] = "Name,Character,20";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(*phConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // 20 rows: NAME00 .. NAME19
    UNSIGNED8 fld[] = "Name";
    for (int i = 0; i < 20; ++i) {
        char val[8];
        std::snprintf(val, sizeof(val), "NAME%02d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, fld,
                             reinterpret_cast<UNSIGNED8*>(val),
                             static_cast<UNSIGNED32>(std::strlen(val)))
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    return hTable;
}

ADSHANDLE create_adi_tag(ADSHANDLE hTable, const char* idxfile) {
    UNSIGNED8 file[64]{};
    std::strncpy(reinterpret_cast<char*>(file), idxfile, sizeof(file) - 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, file, (UNSIGNED8*)"NAME",
                             (UNSIGNED8*)"Name", nullptr, nullptr, 0, 0,
                             &hIdx) == AE_SUCCESS);
    return hIdx;
}

} // namespace

TEST_CASE("ADI keycount: AdsGetKeyCount returns 20 over a 20-row ADI order") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_keycount149";
    { std::error_code ec;
      fs::remove_all(tmp, ec);
      fs::create_directories(tmp, ec); }

    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = make_adi_table(tmp, "kc149.adt", &hConn);
    ADSHANDLE hIdx = create_adi_tag(hTable, "kc149.adi");

    REQUIRE(AdsSetIndexOrderByHandle(hTable, hIdx) == AE_SUCCESS);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &count) == AE_SUCCESS);
    CHECK(count == 20u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}

TEST_CASE("ADI keycount: AdsGetKeyCount honours SET DELETED ON") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_keycount149_del";
    { std::error_code ec;
      fs::remove_all(tmp, ec);
      fs::create_directories(tmp, ec); }

    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = make_adi_table(tmp, "kc149d.adt", &hConn);
    ADSHANDLE hIdx = create_adi_tag(hTable, "kc149d.adi");

    REQUIRE(AdsSetIndexOrderByHandle(hTable, hIdx) == AE_SUCCESS);

    // Delete 5 rows (recnos 6..10).
    for (UNSIGNED32 r = 6; r <= 10; ++r) {
        REQUIRE(AdsGotoRecord(hTable, r) == AE_SUCCESS);
        REQUIRE(AdsDeleteRecord(hTable) == AE_SUCCESS);
    }

    UNSIGNED32 count = 0;

    // SET DELETED OFF: all 20 keys are counted.
    REQUIRE(AdsShowDeleted(1) == AE_SUCCESS);
    REQUIRE(AdsGetKeyCount(hIdx, 0, &count) == AE_SUCCESS);
    CHECK(count == 20u);

    // SET DELETED ON: only the 15 live keys are counted.
    REQUIRE(AdsShowDeleted(0) == AE_SUCCESS);
    REQUIRE(AdsGetKeyCount(hIdx, 0, &count) == AE_SUCCESS);
    CHECK(count == 15u);

    REQUIRE(AdsShowDeleted(1) == AE_SUCCESS);  // restore default
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
