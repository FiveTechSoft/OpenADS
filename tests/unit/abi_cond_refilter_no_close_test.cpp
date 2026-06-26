#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// Issue #87 — conditional re-create IN PLACE while the bag stays OPEN.
//
// The reporter's abi_conditional_index_refilter_test.cpp passes, but it calls
// AdsCloseAllIndexes(hTable) BETWEEN the two creates (modelling rddads'
// ORDLSTCLEAR). In the real rddads path on Windows the client's FERASE of the
// temp .cdx fails because the bag is still open, so that close NEVER happens:
// the second OrdCreate overwrites tag "ORDERX" in an already-open bag. This
// test reproduces THAT path — same bag + same tag, a different key expression
// and FOR, with NO close in between — and asserts the active order rebinds to
// the new tag instead of staying on the previous one.

namespace fs = std::filesystem;

namespace {
UNSIGNED32 ordcreate(ADSHANDLE hTable, const std::string& bag,
                     const char* tag, const char* expr, const char* cond,
                     ADSHANDLE* phIndex) {
    UNSIGNED8 bag_buf[260];
    std::memcpy(bag_buf, bag.c_str(), bag.size() + 1);
    UNSIGNED8 tag_buf[64];   std::memcpy(tag_buf, tag, std::strlen(tag) + 1);
    UNSIGNED8 expr_buf[128]; std::memcpy(expr_buf, expr, std::strlen(expr) + 1);
    UNSIGNED8 cond_buf[128]; std::memcpy(cond_buf, cond, std::strlen(cond) + 1);
    return AdsCreateIndex61(hTable, bag_buf, tag_buf, expr_buf, cond_buf,
                            nullptr, 0, 0, phIndex);
}
} // namespace

TEST_CASE("conditional re-filter char->numeric WITHOUT close (bag stays open)") {
    auto dir = fs::temp_directory_path() / "openads_cond_refilter_noclose";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,6,0;VAL,N,4,0;INC,C,1,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    UNSIGNED8 fName[] = "NAME", fVal[] = "VAL", fInc[] = "INC";
    const char* names[5] = {"PERA",  "MANGO", "UVA",   "KIWI",  "FRESA"};
    const double vals[5] = {10,      20,      30,      40,      50};
    const char*  incs[5] = {"Y",     "N",     "Y",     "N",     "Y"};
    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetString(hTable, fName, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(names[i])),
            static_cast<UNSIGNED32>(std::strlen(names[i])));
        AdsSetDouble(hTable, fVal, vals[i]);
        AdsSetString(hTable, fInc, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(incs[i])), 1);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    const auto bag = (dir / "filtmp.cdx").string();

    // rddads navigates through the ORDER handle it gets back from OrdCreate
    // (pArea->hOrdCurrent = hIndex; AdsGotoTop(hOrdCurrent ? : hTable)), NOT
    // through the table handle. So mirror that exactly: move the cursor with
    // the index handle. (record fields are still read off the table handle.)

    // -- Filter A: ORDERX = NAME (char) FOR INC='Y' → r1 r3 r5, NAME order.
    ADSHANDLE hA = 0;
    REQUIRE(ordcreate(hTable, bag, "ORDERX", "NAME", "INC='Y'", &hA) == 0);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hA, 0, &kc) == 0);
    CHECK(kc == 3u);
    REQUIRE(AdsGotoTop(hA) == 0);                       // navigate via order handle
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
    CHECK(rn == 5u);                                   // FRESA sorts first

    // rddads' ORDLSTCLEAR (OrdCondSet ...,.T.) closes index bindings before the
    // re-create. The temp .cdx file persists on disk (Windows FERASE fails while
    // open), so the second OrdCreate overwrites tag ORDERX in place.
    REQUIRE(AdsCloseAllIndexes(hTable) == 0);

    // -- Filter B on the SAME bag + SAME tag: ORDERX = STR(VAL,8) FOR VAL>=20
    //    → r2..r5, VAL order 20,30,40,50.
    ADSHANDLE hB = 0;
    REQUIRE(ordcreate(hTable, bag, "ORDERX", "STR(VAL,8)", "VAL>=20", &hB) == 0);

    UNSIGNED32 kcB = 0;
    REQUIRE(AdsGetKeyCount(hB, 0, &kcB) == 0);
    CHECK(kcB == 4u);                                  // new FOR matched 4

    // The active order must now navigate Filter B (VAL), not the stale A (NAME).
    // Navigate via the NEW order handle, exactly as rddads does.
    const UNSIGNED32 expect_recno[4] = {2, 3, 4, 5};   // VAL 20,30,40,50
    REQUIRE(AdsGotoTop(hB) == 0);
    for (int i = 0; i < 4; ++i) {
        UNSIGNED32 r = 0, kn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
        REQUIRE(AdsGetKeyNum(hB, 0, &kn) == 0);
        CHECK(r == expect_recno[i]);
        CHECK(kn == static_cast<UNSIGNED32>(i + 1));
        if (i < 3) REQUIRE(AdsSkip(hB, 1) == 0);
    }
    UNSIGNED16 eof = 0;
    REQUIRE(AdsSkip(hB, 1) == 0);
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 1);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx == 1u);                                 // overwrite, not append

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Issue #87 (residual) — rddads' OrdKeyCount (DBOI_KEYCOUNT) does NOT call
// AdsGetKeyCount; it calls AdsGetRecordCount(hOrdCurrent, ADS_RESPECTSCOPES)
// with the ORDER handle. For a conditional/FOR order the index holds only the
// matching rows, so that count must be the key count (4), like native DBFCDX —
// not the table's physical record_count() (5). AdsGetKeyCount already returns 4
// here; AdsGetRecordCount on the same order handle wrongly returns 5.
TEST_CASE("AdsGetRecordCount on a conditional ORDER handle counts only FOR matches") {
    auto dir = fs::temp_directory_path() / "openads_cond_reccount";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,6,0;VAL,N,4,0;INC,C,1,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    UNSIGNED8 fName[] = "NAME", fVal[] = "VAL", fInc[] = "INC";
    const char* names[5] = {"PERA",  "MANGO", "UVA",   "KIWI",  "FRESA"};
    const double vals[5] = {10,      20,      30,      40,      50};
    const char*  incs[5] = {"Y",     "N",     "Y",     "N",     "Y"};
    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetString(hTable, fName, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(names[i])),
            static_cast<UNSIGNED32>(std::strlen(names[i])));
        AdsSetDouble(hTable, fVal, vals[i]);
        AdsSetString(hTable, fInc, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(incs[i])), 1);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    const auto bag = (dir / "filtmp.cdx").string();

    // STR(VAL,8) FOR VAL>=20 -> 4 matching rows (excludes PERA, VAL 10).
    ADSHANDLE hB = 0;
    REQUIRE(ordcreate(hTable, bag, "ORDERX", "STR(VAL,8)", "VAL>=20", &hB) == 0);

    // AdsGetKeyCount already respects the FOR (4).
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hB, 0, &kc) == 0);
    CHECK(kc == 4u);

    // rddads' OrdKeyCount path: AdsGetRecordCount on the ORDER handle must
    // ALSO report 4, not the table's physical 5.
    UNSIGNED32 rcIdx = 0;
    REQUIRE(AdsGetRecordCount(hB, ADS_RESPECTSCOPES, &rcIdx) == 0);
    CHECK(rcIdx == 4u);

    // But AdsGetRecordCount on the TABLE handle still reports the full 5
    // (RecCount() semantics must be unaffected).
    UNSIGNED32 rcTbl = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_RESPECTFILTERS, &rcTbl) == 0);
    CHECK(rcTbl == 5u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// rddads' OTHER OrdCreate path (fClose=TRUE, no ORDLSTCLEAR): after the
// AdsCreateIndex61 overwrite it re-opens the bag with AdsOpenIndex and sets
// hOrdCurrent = ahIndex[0], then navigates through THAT handle. The reporter's
// hypothesis is that re-opening an already-open bag hands back metadata keyed
// by bag+tag that was "reused rather than refreshed" — a stale order. Probe it:
// overwrite in place WITHOUT closing, then AdsOpenIndex and navigate via the
// re-opened handle.
TEST_CASE("conditional re-create then AdsOpenIndex on the still-open bag") {
    auto dir = fs::temp_directory_path() / "openads_cond_reopen";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "NAME,C,6,0;VAL,N,4,0;INC,C,1,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    UNSIGNED8 fName[] = "NAME", fVal[] = "VAL", fInc[] = "INC";
    const char* names[5] = {"PERA",  "MANGO", "UVA",   "KIWI",  "FRESA"};
    const double vals[5] = {10,      20,      30,      40,      50};
    const char*  incs[5] = {"Y",     "N",     "Y",     "N",     "Y"};
    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetString(hTable, fName, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(names[i])),
            static_cast<UNSIGNED32>(std::strlen(names[i])));
        AdsSetDouble(hTable, fVal, vals[i]);
        AdsSetString(hTable, fInc, reinterpret_cast<UNSIGNED8*>(
            const_cast<char*>(incs[i])), 1);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    const auto bag = (dir / "filtmp.cdx").string();

    ADSHANDLE hA = 0;
    REQUIRE(ordcreate(hTable, bag, "ORDERX", "NAME", "INC='Y'", &hA) == 0);
    // overwrite in place, bag stays OPEN (no AdsCloseAllIndexes):
    ADSHANDLE hB = 0;
    REQUIRE(ordcreate(hTable, bag, "ORDERX", "STR(VAL,8)", "VAL>=20", &hB) == 0);

    // rddads re-opens the bag and navigates via the re-opened handle.
    ADSHANDLE ahIndex[256];
    UNSIGNED16 usArrayLen = 256;
    UNSIGNED8 bag_buf[260];
    std::memcpy(bag_buf, bag.c_str(), bag.size() + 1);
    UNSIGNED32 rc = AdsOpenIndex(hTable, bag_buf, ahIndex, &usArrayLen);
    REQUIRE((rc == 0 /*AE_SUCCESS*/ || rc == 5021 /*AE_INDEX_ALREADY_OPEN*/));
    REQUIRE(usArrayLen >= 1u);
    ADSHANDLE hCur = ahIndex[0];

    // navigating via the re-opened handle must reflect Filter B (VAL), not A.
    const UNSIGNED32 expect_recno[4] = {2, 3, 4, 5};   // VAL 20,30,40,50
    REQUIRE(AdsGotoTop(hCur) == 0);
    for (int i = 0; i < 4; ++i) {
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
        CHECK(r == expect_recno[i]);
        if (i < 3) REQUIRE(AdsSkip(hCur, 1) == 0);
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
