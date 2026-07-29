// Counting live keys must not re-read the table through goto_record().
//
// AdsGetKeyCount / AdsGetRecordCount over an order exclude deleted rows while
// SET DELETED is ON by walking the cached index and testing each recno. The
// test went through goto_record(), which invalidates the driver's read-ahead
// on every call, so a sequential count became one block read per record — and
// nothing was cached between calls. FiveWin's TXBrowse evaluates KeyCount()
// several times while opening a browse, so a maintenance screen over a large
// table paid seconds for the count alone.
//
// This test pins the observable contract of the replacement
// (Table::deleted_at): the counts are the same as before, and counting does
// NOT move the cursor. The speed itself is not asserted — a timing threshold
// would be flaky on CI — but the cursor invariant is exactly what the old
// implementation had to save and restore, and it is what breaks first if the
// walk goes back to navigating.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("keycount: counting live keys leaves the cursor where it was") {
    fs::path tmp = fs::temp_directory_path() / "openads_keycount_scan";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "cnt.dbf";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    const std::uint32_t kRows = 40;
    for (std::uint32_t i = 0; i < kRows; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char cod[16];
        std::snprintf(cod, sizeof(cod), "C%08u", i);
        AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)cod,
                     (UNSIGNED32)std::strlen(cod));
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, (UNSIGNED8*)"", (UNSIGNED8*)"TCODIGO",
                             (UNSIGNED8*)"CCODIGO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);

    // Delete the first 10 rows in key order.
    AdsShowDeleted(1);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsDeleteRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }

    SUBCASE("counts are unchanged") {
        UNSIGNED32 kc = 0;

        AdsShowDeleted(1);            // SET DELETED OFF
        REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == AE_SUCCESS);
        CHECK(kc == kRows);

        AdsShowDeleted(0);            // SET DELETED ON
        REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == AE_SUCCESS);
        CHECK(kc == kRows - 10);
    }

    SUBCASE("counting does not move the cursor") {
        AdsShowDeleted(0);            // SET DELETED ON -> the filtering path
        REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
        REQUIRE(AdsSkip(hTable, 3) == AE_SUCCESS);

        UNSIGNED32 before = 0;
        REQUIRE(AdsGetRecordNum(hTable, ADS_IGNOREFILTERS, &before)
                == AE_SUCCESS);

        UNSIGNED32 kc = 0;
        REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == AE_SUCCESS);
        CHECK(kc == kRows - 10);

        UNSIGNED32 after = 0;
        REQUIRE(AdsGetRecordNum(hTable, ADS_IGNOREFILTERS, &after)
                == AE_SUCCESS);
        CHECK(after == before);

        // And the record buffer still belongs to that row, not to whatever
        // the walk happened to read last.
        UNSIGNED8  buf[32]{};
        UNSIGNED32 len = sizeof(buf);
        REQUIRE(AdsGetString(hTable, (UNSIGNED8*)"CCODIGO", buf, &len, 0)
                == AE_SUCCESS);
        std::string cod(reinterpret_cast<char*>(buf), len);
        while (!cod.empty() && cod.back() == ' ') cod.pop_back();
        char expect[16];
        std::snprintf(expect, sizeof(expect), "C%08u", before - 1);
        CHECK(cod == expect);
    }

    AdsShowDeleted(0);
    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}
