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

// Regression: a compound bag that was never populated (created while the
// table was empty, records appended afterwards with the tag unbound) has no
// root page. Opening it over a NON-empty table used to hand the caller a
// ghost order: every GotoTop landed in Limbo (bof=eof=1) and no nav rescue
// escaped (Pritpal Bedi's .z01 work bags over the wire, Aug 2026).
// AdsOpenIndex must now detect the provably-empty unconditional tag and
// reindex the bag once on open.
TEST_CASE("stale empty bag over non-empty table is healed on AdsOpenIndex") {
    auto dir = fs::temp_directory_path() / "openads_stale_bag_heal";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "healme";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    // Build the tag while the table is still EMPTY -> bag with no keys.
    // Non-structural name so a later plain open never auto-binds it.
    auto idx_path = (dir / "ix.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64]  = "BYID";
    UNSIGNED8 expr[64] = "ID";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0,
                           &hIndex) == 0);
    REQUIRE(AdsCloseIndex(hIndex) == 0);

    // Append with the tag unbound: the bag stays at 0 keys (stale).
    UNSIGNED8 fld[] = "ID";
    const double ids[5] = {10, 20, 30, 5, 15};
    for (int i = 0; i < 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, ids[i]);
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    // Reopen the stale bag: the heal must reindex it on open.
    ADSHANDLE idx_handles[64] = {0};
    UNSIGNED16 idx_count = 64;
    REQUIRE(AdsOpenIndex(hTable, idx_buf, idx_handles, &idx_count) == 0);
    REQUIRE(idx_count >= 1);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, idx_handles[0]) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED16 bof = 1, eof = 1;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(bof == 0);   // RED before the heal: bof=eof=1 (Limbo)
    CHECK(eof == 0);

    // Ascending key order: 5 10 15 20 30.
    const double want[5] = {5, 10, 15, 20, 30};
    int visited = 0;
    while (eof == 0 && visited < 50) {
        double v = 0;
        REQUIRE(AdsGetDouble(hTable, fld, &v) == 0);
        REQUIRE(visited < 5);
        CHECK(v == want[visited]);
        ++visited;
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(visited == 5);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Regression: a PARTIALLY-stale bag — 3 keys over 5 records (appends done
// with the bag closed, killed before flush, or SQL DML on a custom-ext
// bag). Since 60841ec the one-shot open heal triggers ONLY on an empty bag
// (root_page == 0): a key_count != record_count trigger caused a
// reindex convoy with 700 concurrent openers under the storm-700 load.
// A partially stale bag must therefore stay as-is: the stale key entries
// remain visible to the walk until the app reindexes explicitly.
TEST_CASE("partially stale bag is healed on AdsOpenIndex") {
    auto dir = fs::temp_directory_path() / "openads_stale_bag_partial";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "partme";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    auto idx_path = (dir / "ixp.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64]  = "BYID";
    UNSIGNED8 expr[64] = "ID";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0,
                           &hIndex) == 0);

    // 3 records WITH the bag bound -> 3 keys.
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 3; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, static_cast<double>(i));
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsCloseIndex(hIndex) == 0);

    // 2 more records with the bag CLOSED -> stale at 3/5 keys.
    for (int i = 4; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, static_cast<double>(i));
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    ADSHANDLE idx_handles[64] = {0};
    UNSIGNED16 idx_count = 64;
    REQUIRE(AdsOpenIndex(hTable, idx_buf, idx_handles, &idx_count) == 0);
    REQUIRE(idx_count >= 1);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, idx_handles[0]) == 0);

    // No heal on a partially stale bag (by design since 60841ec): the
    // 3 bound-era keys are all the walk sees; GoBottom lands on the last
    // stale key (ID 3). Records 4-5 are invisible to the order until the
    // app reindexes.
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hTable, 0, &kc) == 0);
    CHECK(kc == 3u);
    REQUIRE(AdsGotoBottom(hTable) == 0);
    double v = 0;
    REQUIRE(AdsGetDouble(hTable, fld, &v) == 0);
    CHECK(v == 3.0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Counter-case for the heal: a CONDITIONAL tag (FOR) that matches zero rows
// is legitimately empty — opening it must NOT trigger a reindex, and the
// order stays in Limbo by design.
TEST_CASE("conditional tag with zero matching rows is not healed") {
    auto dir = fs::temp_directory_path() / "openads_stale_bag_noheal";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,4,0";
    UNSIGNED8 tname[] = "condme";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hTable) == 0);

    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 3; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == 0);
        AdsSetDouble(hTable, fld, static_cast<double>(i));
    }
    REQUIRE(AdsWriteRecord(hTable) == 0);

    // FOR matches nothing -> conditional bag with 0 keys by design.
    auto idx_path = (dir / "ixc.cdx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);
    UNSIGNED8 tag[64]  = "NONE";
    UNSIGNED8 expr[64] = "ID";
    UNSIGNED8 cond[64] = "ID > 999";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, cond, 0, 0,
                           &hIndex) == 0);
    REQUIRE(AdsCloseIndex(hIndex) == 0);

    ADSHANDLE idx_handles[64] = {0};
    UNSIGNED16 idx_count = 64;
    REQUIRE(AdsOpenIndex(hTable, idx_buf, idx_handles, &idx_count) == 0);
    REQUIRE(idx_count >= 1);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, idx_handles[0]) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED16 bof = 0, eof = 0;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(bof == 1);   // still Limbo: the heal must leave it alone
    CHECK(eof == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
