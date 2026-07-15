// M12.21 option C — sequential prefetch with consumed-counter sync.
//
// The wire protocol can piggyback a lookahead block of rows on a
// forward Skip ack so a sequential scan drains a client-side queue
// instead of paying one TCP round-trip per record. It was shipped
// disabled because consuming the queue locally desynced the server
// cursor (a Skip(-1) after K drained rows read the wrong row).
//
// These tests pin the re-enabled behavior end-to-end (ABI client over
// a real loopback socket to an in-process server):
//   1. a forward scan returns the correct recnos AND issues far fewer
//      server requests than records (prefetch is actually active, and
//      the consumed-counter keeps the cursor correct across queue
//      drains, since N > the server lookahead window);
//   2. mixed forward/backward navigation lands on the right recno
//      (the consumed-counter resync is correct in both directions).
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include "mgmt/mg_stats.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Create an N-record single-field DBF in `dir` (recno == ID == 1..N).
void make_dbf(const fs::path& dir, const char* tbl, int n) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]  = "ID,N,8,0;VAL,N,6,0";
    std::vector<UNSIGNED8> tname(tbl, tbl + std::strlen(tbl) + 1);
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname.data(), nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= n; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetDouble(hT, fld, static_cast<double>(i));
    }
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

// Connect the ABI client to an in-process server over loopback.
ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(hConn != 0);
    return hConn;
}

} // namespace

TEST_CASE("remote prefetch: forward scan is correct and round-trip-thrifty") {
    using openads::network::Server;
    const int N = 300;                 // > server lookahead window
    auto dir = fs::temp_directory_path() / "openads_prefetch_fwd";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r0) == 0);
    CHECK(r0 == 1u);

    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    std::vector<UNSIGNED32> seen;
    seen.reserve(N);
    for (int k = 2; k <= N; ++k) {
        REQUIRE(AdsSkip(hTable, 1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        seen.push_back(rn);
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    // Correctness: the scan visited recno 2..N in order, even across
    // queue drains (a wrong consumed-counter would skip/repeat here).
    bool ordered = true;
    for (int k = 2; k <= N; ++k) {
        if (seen[static_cast<std::size_t>(k - 2)] != static_cast<UNSIGNED32>(k)) {
            ordered = false;
        }
    }
    CHECK(ordered);

    // Prefetch active: a 299-step scan must cost far fewer than 299
    // server requests (each lookahead block serves many steps from the
    // client cache). Generous bound to stay robust to the window size.
    CHECK(reqs <= 30u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote prefetch: single Skip(+1)/Skip(-1) roundtrip") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_prefetch_rt1";
    make_dbf(dir, "pf", 50);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r0) == 0);
    CHECK(r0 == 1u);

    // First forward skip may drain a GotoTop lookahead row; the very
    // next backward skip must land back on the same record.
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsSkip(hTable, -1) == 0);
    UNSIGNED32 r1 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r1) == 0);
    CHECK(r1 == 1u);

    // Same after the prefetch queue has been populated.
    for (int i = 0; i < 5; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);
    UNSIGNED32 r6 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r6) == 0);
    CHECK(r6 == 6u);
    REQUIRE(AdsSkip(hTable, -1) == 0);
    UNSIGNED32 r5 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r5) == 0);
    CHECK(r5 == 5u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote prefetch: mixed forward/backward lands on the right recno") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_prefetch_mixed";
    make_dbf(dir, "pf", 50);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);          // recno 1
    for (int i = 0; i < 4; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);   // -> 5
    UNSIGNED32 r5 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r5) == 0);
    CHECK(r5 == 5u);

    REQUIRE(AdsSkip(hTable, -1) == 0);          // -> 4
    REQUIRE(AdsSkip(hTable, -1) == 0);          // -> 3
    UNSIGNED32 r3 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r3) == 0);
    CHECK(r3 == 3u);

    REQUIRE(AdsSkip(hTable, 1) == 0);           // -> 4
    UNSIGNED32 r4 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r4) == 0);
    CHECK(r4 == 4u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote prefetch: a write mid-scan hits the logical record, not the lagging cursor") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_prefetch_write";
    make_dbf(dir, "pf", 100);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    // Scan forward to recno 10. The first Skip primes the lookahead
    // queue; the next 8 Skips are served locally, so the server cursor
    // lags at recno 2 while the client is logically at recno 10.
    REQUIRE(AdsGotoTop(hTable) == 0);
    for (int i = 0; i < 9; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
    REQUIRE(rn == 10u);

    // Write the current record. Without the consumed-counter resync this
    // lands on the lagging server cursor (recno 2), corrupting the wrong
    // row.
    // Since 6df3f38a, writes to a pre-existing (non-append) record require
    // an explicit lock — AdsLockRecord takes an absolute recno and is
    // forwarded straight to the server, independent of cursor position.
    UNSIGNED8 valf[] = "VAL";
    REQUIRE(AdsLockRecord(hTable, 10) == 0);
    REQUIRE(AdsSetDouble(hTable, valf, 999.0) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);

    // recno 10 must carry the value; recno 2 (the lagging cursor) must not.
    double v10 = 0.0, v2 = 0.0;
    REQUIRE(AdsGotoRecord(hTable, 10) == 0);
    REQUIRE(AdsGetDouble(hTable, valf, &v10) == 0);
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsGetDouble(hTable, valf, &v2) == 0);
    CHECK(v10 == 999.0);
    CHECK(v2 == 0.0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// RCB 07/14/2026 — M12.23. AdsCacheRecords(hTable, N) now actually reaches the
// server, as an optional trailing [u16] on the Skip request. Two cases matter,
// and they are the two SAP calls out in ace_adscacherecords.htm.
//
// Both assert on REQUEST COUNTS, which is the only way to observe this: a hint
// changes how much comes back per round-trip, never what the rows are. Relax
// these bounds and the API goes back to being a no-op with nothing to notice.
TEST_CASE("AdsCacheRecords(0) turns read-ahead off") {
    using openads::network::Server;
    const int N = 60;
    auto dir = fs::temp_directory_path() / "openads_cacherecs_off";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    // SAP: "A usRecords value of 0 (or 1) effectively turns read-ahead record
    // caching off." The documented use is a batch loop that edits most of the
    // records it visits, where every edit would dump the block anyway.
    REQUIRE(AdsCacheRecords(hTable, 0) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    bool ordered = true;
    for (int k = 2; k <= N; ++k) {
        REQUIRE(AdsSkip(hTable, 1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        if (rn != static_cast<UNSIGNED32>(k)) ordered = false;
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(ordered);                 // still correct, just uncached
    // Caching off => one wire Skip per record. (With read-ahead on, the same
    // scan costs a handful of requests — see the forward-scan test above.)
    CHECK(reqs >= static_cast<std::uint64_t>(N - 5));

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("AdsCacheRecords(100) reads ahead aggressively") {
    using openads::network::Server;
    const int N = 300;
    auto dir = fs::temp_directory_path() / "openads_cacherecs_aggr";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    // SAP's rcAggressive == 100 records. An explicit depth overrides the
    // server's automatic ramp, so blocks are 100 rows from the very first
    // Skip rather than climbing 8 -> 16 -> 32 -> 64.
    REQUIRE(AdsCacheRecords(hTable, 100) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    bool ordered = true;
    for (int k = 2; k <= N; ++k) {
        REQUIRE(AdsSkip(hTable, 1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        if (rn != static_cast<UNSIGNED32>(k)) ordered = false;
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(ordered);
    // 299 steps / 100-row blocks = 3 wire skips. Beats the ramped default
    // (which spends its first few blocks climbing), so this is a real,
    // observable difference — not just "still fast".
    CHECK(reqs <= 6u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// RCB 07/14/2026 — read-ahead x SET DELETED (M12.31/M12.32) interaction.
//
// These are separate features that meet badly. Flipping SET DELETED changes
// which rows are VISIBLE, and the look-ahead block holds rows already read
// AHEAD of the cursor under the old visibility. Without an explicit
// invalidation the scan keeps serving them — locally, with no wire traffic at
// all to hint anything is stale — so deleted records keep showing up after
// SET DELETED ON. The block has to be dropped when the meaning of its rows
// changes, exactly as for Seek and order changes.
TEST_CASE("remote prefetch: flipping SET DELETED drops the read-ahead block") {
    using openads::network::Server;
    const int N = 60;
    auto dir = fs::temp_directory_path() / "openads_prefetch_showdel";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    // Delete every even record while deleted rows are visible.
    REQUIRE(AdsShowDeleted(1) == 0);
    for (int k = 2; k <= N; k += 2) {
        REQUIRE(AdsGotoRecord(hTable, static_cast<UNSIGNED32>(k)) == 0);
        REQUIRE(AdsLockRecord(hTable, static_cast<UNSIGNED32>(k)) == 0);
        REQUIRE(AdsDeleteRecord(hTable) == 0);
    }

    // Land on recno 1 with deleted rows still VISIBLE. This is the setup that
    // makes the test discriminating, and it is worth being explicit about:
    // the block always holds the rows AFTER the cursor, so sitting on an ODD
    // recno means the queued front row is recno 2 — a DELETED one. (Since
    // M12.24 the GotoTop ack itself arrives carrying that block.)
    //
    // Do NOT reposition after the flip below. A GotoTop/Seek would refetch the
    // block and mask the bug; the whole hazard is a skip that never reaches the
    // wire and is served straight out of the stale queue.
    REQUIRE(AdsShowDeleted(1) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 r1 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r1) == 0);
    REQUIRE(r1 == 1u);

    // Now hide deleted rows. Every row already queued was read under the old
    // visibility, and the very next one is deleted.
    REQUIRE(AdsShowDeleted(0) == 0);

    // The next step must skip OVER the deleted recno 2 and land on 3. Without
    // the invalidation this pops recno 2 — a deleted record — out of the client
    // cache with no wire traffic at all.
    REQUIRE(AdsSkip(hTable, 1) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
    UNSIGNED16 del = 9;
    REQUIRE(AdsIsRecordDeleted(hTable, &del) == 0);
    CHECK(rn == 3u);                       // NOT 2
    CHECK(del == 0);                       // and not a deleted record

    // Carry on to the end: only live (odd) recnos, exactly half the table.
    int seen = 2;                          // recnos 1 and 3 so far
    bool any_deleted = false, any_even = false;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (!eof && seen < N) {
        UNSIGNED32 r = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &r) == 0);
        UNSIGNED16 d = 9;
        REQUIRE(AdsIsRecordDeleted(hTable, &d) == 0);
        if (d != 0)     any_deleted = true;
        if (r % 2 == 0) any_even    = true;
        ++seen;
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }

    CHECK_FALSE(any_deleted);              // no deleted row ever served
    CHECK_FALSE(any_even);                 // the even recnos ARE the deleted ones
    CHECK(seen == N / 2);                  // exactly the live half

    REQUIRE(AdsShowDeleted(1) == 0);       // restore process-wide default
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote prefetch: an Eof()/IsFound()-polling scan loop sheds its round-trips") {
    using openads::network::Server;
    const int N = 300;
    auto dir = fs::temp_directory_path() / "openads_prefetch_loop";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    // The rddads scan shape: poll Eof() and IsFound() every step. With
    // the caches both answer locally, so the loop's only server traffic
    // is the periodic lookahead refill.
    int seen = 0;
    bool ordered = true, found_clear = true;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (!eof) {
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        ++seen;
        if (rn != static_cast<UNSIGNED32>(seen)) ordered = false;
        UNSIGNED16 fnd = 9;
        REQUIRE(AdsIsFound(hTable, &fnd) == 0);
        if (fnd != 0) found_clear = false;          // a plain scan never "finds"
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(seen == N);              // visited every record
    CHECK(ordered);                // in order
    CHECK(found_clear);            // Found() stayed false through the scan
    CHECK(reqs <= 30u);            // Eof()/IsFound()/Skip served locally

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// RCB 07/15/2026 — M12.25: BACKWARD (PgUp) prefetch. Forward prefetch shipped
// in M12.21; a backward scan still paid one round-trip per Skip(-1). These
// tests pin the symmetric backward path end-to-end. They matter as REQUEST
// COUNTS (backward read-ahead is active only if a long backward scan costs far
// fewer than N requests) AND as recno CHECKS (a backward block walked the wrong
// way, or an off-by-one restore at the BoF boundary, shows up as a wrong recno).
TEST_CASE("remote prefetch: backward scan is correct and round-trip-thrifty") {
    using openads::network::Server;
    const int N = 300;
    auto dir = fs::temp_directory_path() / "openads_prefetch_back";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    REQUIRE(AdsGotoBottom(hTable) == 0);      // recno N
    UNSIGNED32 r0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r0) == 0);
    CHECK(r0 == static_cast<UNSIGNED32>(N));

    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    bool ordered = true;
    for (int k = N - 1; k >= 1; --k) {
        REQUIRE(AdsSkip(hTable, -1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        if (rn != static_cast<UNSIGNED32>(k)) ordered = false;
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(ordered);            // walked N-1, N-2, ..., 1
    CHECK(reqs <= 30u);        // backward read-ahead active; measured 7, was ~299

    // One more Skip(-1) from recno 1 must land at BoF, not repeat recno 1.
    REQUIRE(AdsSkip(hTable, -1) == 0);
    UNSIGNED16 bof = 0;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    CHECK(bof != 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote prefetch: reversal (PgDn then PgUp) lands on the right recno") {
    using openads::network::Server;
    const int N = 200;
    auto dir = fs::temp_directory_path() / "openads_prefetch_reversal";
    make_dbf(dir, "pf", N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[16] = "pf.dbf";
    UNSIGNED8 alias[8]  = "pf";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    auto at = [&](int want) {
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        CHECK(rn == static_cast<UNSIGNED32>(want));
    };

    REQUIRE(AdsGotoTop(hTable) == 0);                 // 1
    for (int i = 0; i < 20; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);   // -> 21
    at(21);
    // Reverse. The first backward step can't be served from the forward queue
    // (the resync fold keeps the cursor correct), so it goes to the wire and
    // primes a backward block; the rest drain locally. Correctness is the point.
    for (int i = 0; i < 15; ++i) REQUIRE(AdsSkip(hTable, -1) == 0);  // -> 6
    at(6);
    // Reverse again.
    for (int i = 0; i < 10; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);   // -> 16
    at(16);
    // And a longer backward run to exercise a full backward block refill.
    for (int i = 0; i < 12; ++i) REQUIRE(AdsSkip(hTable, -1) == 0);  // -> 4
    at(4);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}
