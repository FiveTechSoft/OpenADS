#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// Regression: an index walk must SKIP entries whose recno is stale
// (recno > record_count) instead of raising ADSCDX/5000 "record number out
// of range". A PACK that drops rows leaves any tag that was NOT bound at
// PACK time pointing at recnos past the compacted count; native ADSCDX walks
// past those phantom entries, OpenADS used to propagate 5000 and crash a
// DELETE/REPLACE ... FOR (DBEVAL) that hadn't pinned a fresh order first
// (the ERP _INDEXAR workaround was DBSETORDER(1) before the DELETE FOR).
//
// Repro: build a NON-structural index (ix.cdx, so opening the table never
// auto-binds it and PACK leaves it stale on disk), close it, drop rows that
// land at both ends of the key order + PACK, reopen the now-stale tag, then
// GOTOP and walk forward. The walk must visit exactly the survivors and
// never see 5000.

namespace fs = std::filesystem;

TEST_CASE("index walk skips stale entries left by a PACK (no ADSCDX/5000)") {
    auto dir = fs::temp_directory_path() / "openads_stale_index_walk";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    // recno -> ID, chosen so the BYID key order interleaves the dropped rows:
    //   rec1=10  rec2=20  rec3=30  rec4=5  rec5=15
    // BYID order: 5(rec4) 10(rec1) 15(rec5) 20(rec2) 30(rec3)
    // Dropping rec4 (smallest key) and rec5 (a middle key) leaves a stale
    // entry FIRST in the order (exercises goto_top's skip) and another mid
    // walk (exercises skip's).
    UNSIGNED8 fld[] = "ID";
    const double ids[5] = {10, 20, 30, 5, 15};
    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, ids[i]);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    // NON-structural index name (ix.cdx != data.cdx) so opening the table
    // never auto-binds it — that's what lets PACK leave it stale.
    auto idx_path = (dir / "ix.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64]  = "BYID";
    UNSIGNED8 expr[64] = "ID";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0, &hIndex) == 0);

    // Unbind the index so the upcoming PACK does NOT rebuild it.
    REQUIRE(AdsCloseIndex(hIndex) == 0);

    // Drop rec4 (key 5) and rec5 (key 15) -> 3 survivors renumbered 1..3.
    // ix.cdx still maps 5->rec4 and 15->rec5 (now stale: recno > 3).
    REQUIRE(AdsGotoRecord(hTable, 4) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    REQUIRE(AdsGotoRecord(hTable, 5) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsPackTable(hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 3u);

    // Reopen the now-stale tag and make it the active order.
    ADSHANDLE idx_handles[64] = {0};
    UNSIGNED16 idx_count = 64;
    REQUIRE(AdsOpenIndex(hTable, idx_buf, idx_handles, &idx_count) == 0);
    REQUIRE(idx_count >= 1);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, idx_handles[0]) == 0);

    // GOTOP: the smallest key (5) points at the stale rec4. Before the fix
    // this returned 5000; it must now skip to the first live entry.
    REQUIRE(AdsGotoTop(hTable) == 0);            // RED: 5000 here

    int visited = 0;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (eof == 0 && visited < 50) {
        double v = 0;
        REQUIRE(AdsGetDouble(hTable, fld, &v) == 0);   // RED: 5000 on a stale row
        ++visited;
        REQUIRE(AdsSkip(hTable, 1) == 0);              // RED: 5000 stepping onto stale
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(visited == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
