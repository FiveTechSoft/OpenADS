#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// Regression coverage for the NTX PACK/reindex heap-corruption crash.
//
// reindex() (driven by PACK) empties every bound index via a key-by-key
// erase walk, then re-inserts the survivors. NtxIndex::erase used a
// swap-to-tail free-slot scheme that, once a leaf reached key_count == 0,
// left offset[0] pointing at the *last-removed* key's physical slot rather
// than the pristine first slot. The subsequent re-insert into that
// empty-but-rooted leaf takes offset[kc] as its free slot and marches the
// free pointer forward from there -- off the end of the 1024-byte page once
// enough keys are re-inserted. That overruns the fixed Page buffer
// (heap corruption), surfacing as 6106 "short read on NTX page" and then a
// crash inside flush() on AdsCloseTable.
//
// The 4-record single-page abi_ntx_pack_reindex_test never grows past one
// leaf, so it cannot reach the overrun. This test builds a multi-page NTX
// (hundreds of keys), deletes a clustered block so reindex empties whole
// leaves, and PACKs -- which returned 6106 / crashed before the fix.

namespace fs = std::filesystem;

TEST_CASE("PACK reindexes a multi-page NTX with emptied leaves (no heap overrun)") {
    auto dir = fs::temp_directory_path() / "openads_ntx_pack_reindex_multipage";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "K,C,12,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_NTX,
                           0, 0, 0, 0, def, &hTable) == 0);

    // 400 records: every 5th carries an empty key (all-blank -> identical,
    // they cluster together in the leftmost leaves); the rest are unique.
    const int N = 400;
    UNSIGNED8 fld[] = "K";
    for (int i = 1; i <= N; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        char key[16];
        if (i % 5 == 0) key[0] = '\0';                       // empty -> blanks
        else std::snprintf(key, sizeof(key), "K%08d", i);    // unique
        REQUIRE(AdsSetString(hTable, fld,
                    reinterpret_cast<UNSIGNED8*>(key),
                    static_cast<UNSIGNED32>(std::strlen(key))) == 0);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    UNSIGNED8 idx_file[32] = "data.ntx";
    UNSIGNED8 tag[16]      = "BYK";
    UNSIGNED8 expr[16]     = "K";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex61(hTable, idx_file, tag, expr,
                             nullptr, nullptr, 0, 0, &hIndex) == 0);

    // Delete the 80 empty-key records (recnos 5,10,...,400). They form a
    // contiguous block at the front of the index, so reindex empties whole
    // leaves when it rebuilds.
    int deleted = 0;
    for (int i = 5; i <= N; i += 5) {
        REQUIRE(AdsGotoRecord(hTable, static_cast<UNSIGNED32>(i)) == 0);
        REQUIRE(AdsDeleteRecord(hTable) == 0);
        ++deleted;
    }
    CHECK(deleted == 80);

    // The defect: this returned 6106 (and AdsCloseTable then crashed).
    REQUIRE(AdsPackTable(hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == static_cast<UNSIGNED32>(N - deleted));   // 320 survivors

    // NOTE: this test pins the crash/heap-overrun fix only. Full multi-page
    // key reachability after reindex (Seek/Skip over every survivor) is a
    // separate, pre-existing NTX limitation tracked apart from this fix.

    // Close must not crash flushing the rebuilt index.
    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
