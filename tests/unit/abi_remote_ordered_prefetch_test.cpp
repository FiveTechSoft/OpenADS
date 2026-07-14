// RCB 07/14/2026 — read-ahead on an ORDERED remote browse (M12.22/M12.24).
//
// WHY THIS FILE EXISTS, before you touch a bound in it: every assertion here is
// a round-trip or byte COUNT, and those counts are the whole point. They are
// not incidental. "remote ordered prefetch: index-order scan" pins a 299-row
// ordered scan at <= 30 wire requests; the same scan cost 598 before this work
// (2 per row: a redundant SetOrder plus the Skip, with the read-ahead block
// thrown away each time). If someone reintroduces a per-row frame, these are
// the tests that will say so — a correctness-only test suite would stay green
// through the entire regression, because the OLD behaviour was slow, not wrong.
//
// The fixture is built so the two orders DISAGREE (see make_reversed_dbf), so a
// block accidentally walked in natural order is caught by the recno checks and
// not merely by a count.
//
// Natural-order prefetch shipped in M12.21 (see abi_remote_prefetch_test.cpp),
// but it was explicitly disabled whenever a controlling order was active: the
// lookahead block was walked on the engine cursor, which only ever moves in
// natural record order, so for an ordered table it would have returned the
// wrong rows. That left the browse that actually matters paying full price —
// rddads navigates on hOrdCurrent (a RemoteIndex handle) whenever an order is
// set (ads1.c: AdsSkip(pArea->hOrdCurrent ? ... : hTable, n)), and every such
// skip also re-sent a SetOrder frame first. Two round-trips per row, with the
// prefetch queue thrown away each time.
//
// These tests pin the fixed behavior end-to-end (ABI client over a real
// loopback socket to an in-process server):
//   1. an index-order scan returns the rows in INDEX order (not natural
//      order) and costs far fewer round-trips than records;
//   2. a Seek does not leave a stale pre-seek row in the queue;
//   3. mixed forward/backward navigation lands on the right record;
//   4. the depth ramps — a lone Skip after a reposition does not drag a full
//      block over the wire.
//
// The fixture is built so the two orders DISAGREE: VAL descends as the recno
// ascends, so an ascending index on VAL walks the records backwards. A
// lookahead block accidentally walked in natural order would therefore be
// caught immediately by the recno checks, not just by a round-trip count.
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

// N records: recno k has ID = k and VAL = N + 1 - k.
// Ascending index on VAL therefore visits recno N, N-1, ..., 1.
void make_reversed_dbf(const fs::path& dir, int n) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "ID,N,8,0;VAL,N,8,0";
    UNSIGNED8 tname[] = "OP.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 f_id[]  = "ID";
    UNSIGNED8 f_val[] = "VAL";
    for (int k = 1; k <= n; ++k) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetDouble(hT, f_id,  static_cast<double>(k));
        AdsSetDouble(hT, f_val, static_cast<double>(n + 1 - k));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    // Production bag, so the remote open auto-binds it.
    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "OP.CDX";
    UNSIGNED8 tag[]  = "BYVAL";
    UNSIGNED8 expr[] = "VAL";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512,
                             &hIndex) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

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

// Open OP.DBF remotely and focus the BYVAL order, returning both handles.
// Mirrors rddads: resolve the tag to an index handle, then navigate on it.
void open_ordered(ADSHANDLE hConn, ADSHANDLE* hTable, ADSHANDLE* hIndex) {
    UNSIGNED8 tname[] = "OP.DBF";
    UNSIGNED8 alias[] = "OP";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX,
                         0, 0, 0, 0, hTable) == 0);
    UNSIGNED8 tag[] = "BYVAL";
    REQUIRE(AdsGetIndexHandle(*hTable, tag, hIndex) == 0);
    REQUIRE(*hIndex != 0);
}

} // namespace

TEST_CASE("remote ordered prefetch: index-order scan is correct and thrifty") {
    using openads::network::Server;
    const int N = 300;                       // >> the lookahead ceiling
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_fwd";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    // Ordered GotoTop lands on the LOWEST VAL — recno N, not recno 1.
    REQUIRE(AdsGotoTop(hIndex) == 0);
    UNSIGNED32 r0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r0) == 0);
    CHECK(r0 == static_cast<UNSIGNED32>(N));

    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    std::vector<UNSIGNED32> seen;
    seen.reserve(N);
    for (int k = 1; k < N; ++k) {
        REQUIRE(AdsSkip(hIndex, 1) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        seen.push_back(rn);
    }
    const std::uint64_t reqs = stats.packets_in.load() - base;

    // Correctness: the scan walked the INDEX (recno N-1, N-2, ... 1). A
    // lookahead block walked in natural order would show ascending recnos.
    bool ordered = true;
    for (int k = 1; k < N; ++k) {
        const auto want = static_cast<UNSIGNED32>(N - k);
        if (seen[static_cast<std::size_t>(k - 1)] != want) ordered = false;
    }
    CHECK(ordered);

    // Thrift: before this change a 299-step ordered scan cost 598 requests
    // (SetOrder + Skip per row). Now the order is set once and each lookahead
    // block serves many steps from the client cache.
    // Measured: 598 requests before this change (SetOrder + Skip per row),
    // 7 after — the ramp's 8+16+32+64+64+64+64 = 312 >= 299 blocks, each one
    // wire skip. The bound is generous so the test survives tuning changes.
    CHECK(reqs <= 30u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote ordered prefetch: Seek does not leave a stale row queued") {
    using openads::network::Server;
    const int N = 100;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_seek";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    // Prime the queue: GotoTop then a few forward steps, so rows are cached.
    REQUIRE(AdsGotoTop(hIndex) == 0);
    for (int i = 0; i < 5; ++i) REQUIRE(AdsSkip(hIndex, 1) == 0);

    // Seek VAL == 50 -> recno N + 1 - 50 == 51.
    const double key = 50.0;
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex, (UNSIGNED8*)&key, sizeof(key),
                    ADS_DOUBLEKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found != 0);
    UNSIGNED32 rs = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rs) == 0);
    CHECK(rs == static_cast<UNSIGNED32>(N + 1 - 50));

    // The step after the seek must be the seek target's INDEX successor
    // (VAL 51 -> recno 50). Before the fix the queue still held pre-seek rows
    // and this Skip popped one of them with no wire traffic at all.
    REQUIRE(AdsSkip(hIndex, 1) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
    CHECK(rn == static_cast<UNSIGNED32>(N + 1 - 51));

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote ordered prefetch: mixed forward/backward lands correctly") {
    using openads::network::Server;
    const int N = 100;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_mixed";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    // Index position p (1-based) == recno N + 1 - p.
    auto recno_at = [&](int p) { return static_cast<UNSIGNED32>(N + 1 - p); };
    auto check_at = [&](int p) {
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
        CHECK(rn == recno_at(p));
    };

    REQUIRE(AdsGotoTop(hIndex) == 0);                     // pos 1
    check_at(1);
    for (int i = 0; i < 9; ++i) REQUIRE(AdsSkip(hIndex, 1) == 0);   // pos 10
    check_at(10);

    // Backward steps must resync the lagging server cursor through the
    // consumed counter — 9 of the 10 steps above were served locally.
    REQUIRE(AdsSkip(hIndex, -1) == 0);                    // pos 9
    check_at(9);
    REQUIRE(AdsSkip(hIndex, -3) == 0);                    // pos 6
    check_at(6);
    REQUIRE(AdsSkip(hIndex, 1) == 0);                     // pos 7
    check_at(7);
    REQUIRE(AdsSkip(hIndex, 5) == 0);                     // pos 12
    check_at(12);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// M12.24 — the first page after a reposition used to be cold.
TEST_CASE("remote ordered prefetch: GotoTop comes back warm") {
    using openads::network::Server;
    const int N = 100;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_warmtop";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    auto& stats = openads::mgmt::process_mg_stats();
    REQUIRE(AdsGotoTop(hIndex) == 0);

    // The GotoTop ack now carries a lookahead block, so the first Skip after it
    // is served from the client cache with NO wire traffic at all. Before
    // M12.24 this cost a full round-trip every time a browse painted its first
    // screen.
    const std::uint64_t base = stats.packets_in.load();
    REQUIRE(AdsSkip(hIndex, 1) == 0);
    CHECK(stats.packets_in.load() == base);          // zero requests

    // ...and it is the right row: index order, so position 2 == recno N-1.
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rn) == 0);
    CHECK(rn == static_cast<UNSIGNED32>(N - 1));

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// M12.24 — SeekAck used to be [u8 found][u32 recno] with no row, so row_valid
// stayed false and the first field read after a seek paid a second round-trip
// (FetchCurrentRow). Seek-then-read is the most common thing an app does.
TEST_CASE("remote ordered prefetch: seek-then-read costs one round-trip") {
    using openads::network::Server;
    const int N = 100;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_seekread";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    // Warm the schema cache so DescribeTable doesn't count against the seek.
    REQUIRE(AdsGotoTop(hIndex) == 0);
    UNSIGNED32 warm = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &warm) == 0);
    double dummy = 0;
    UNSIGNED8 f_id[] = "ID";
    REQUIRE(AdsGetDouble(hTable, f_id, &dummy) == 0);

    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();

    // Seek VAL == 50  ->  recno N + 1 - 50 == 51, whose ID is 51.
    const double key = 50.0;
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex, (UNSIGNED8*)&key, sizeof(key),
                    ADS_DOUBLEKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found != 0);
    double idv = 0;
    REQUIRE(AdsGetDouble(hTable, f_id, &idv) == 0);
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(idv == 51.0);       // the seek landed right AND the row came with it
    CHECK(reqs == 1u);        // one frame: the Seek. Was 2 (Seek + FetchCurrentRow).

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote ordered prefetch: depth ramps instead of a flat block") {
    using openads::network::Server;
    const int N = 300;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_ramp";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    auto& stats = openads::mgmt::process_mg_stats();

    // A single Skip right after a reposition — the "reposition, then read one
    // record" shape. The run is cold, so the server must answer with the FLOOR
    // depth, not a full ceiling block the caller will never read.
    //
    // RCB 07/14/2026: the setup below looks fussy. Both lines are load-bearing;
    // I got each of them wrong once before this test passed.
    //
    // 1. GotoTop FIRST, purely to install the ORDER on the server. Resolving an
    //    index handle (AdsGetIndexHandle) does NOT by itself send a SetOrder, so
    //    without this the GotoRecord below is interpreted in NATURAL order,
    //    lands on the last physical record, and the skip after it walks straight
    //    off the end of the index. The symptom is an 8-byte EOF ack, which reads
    //    as "the ramp is broken" and is not.
    //
    // 2. GotoRecord and NOT GotoTop for the cold measurement itself, because
    //    since M12.24 a GotoTop ack arrives WARM — it carries a block of its own,
    //    so the skip after it is served from the client cache and never reaches
    //    the wire. There would be nothing to weigh. GotoRecord carries the row
    //    but no block, which is the genuinely cold run this test is about.
    //
    // 3. Land on recno N, which is index POSITION 1 — VAL ascending walks the
    //    records BACKWARDS — so the scan below has the whole table ahead of it.
    //    Anchoring mid-file leaves too few rows and the "hot" refills come back
    //    truncated by EOF rather than by the ramp ceiling, which again looks like
    //    a broken ramp.
    REQUIRE(AdsGotoTop(hIndex) == 0);
    REQUIRE(AdsGotoRecord(hTable, static_cast<UNSIGNED32>(N)) == 0);
    const std::uint64_t b0 = stats.bytes_out.load();
    REQUIRE(AdsSkip(hIndex, 1) == 0);
    const std::uint64_t cold_bytes = stats.bytes_out.load() - b0;

    // Now scan hard enough for the ramp to reach the ceiling, and measure a
    // refill at full depth.
    for (int i = 0; i < 200; ++i) REQUIRE(AdsSkip(hIndex, 1) == 0);
    std::uint64_t hot_bytes = 0;
    for (int i = 0; i < 64; ++i) {
        const std::uint64_t b1 = stats.bytes_out.load();
        REQUIRE(AdsSkip(hIndex, 1) == 0);
        const std::uint64_t d = stats.bytes_out.load() - b1;
        if (d > hot_bytes) hot_bytes = d;   // the refill step
    }

    // The cold block must be materially smaller than a hot one. Both are
    // non-zero (each Skip that reaches the wire carries at least the current
    // row), so this is a real ramp check, not a tautology.
    CHECK(cold_bytes > 0u);
    CHECK(hot_bytes > cold_bytes * 2u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// RCB 07/15/2026 — a SEEK must reset the depth ramp, like any other reposition.
//
// The run-breaker keys prefetch_depth_ by TABLE id, but a Seek frame leads with
// an INDEX id. Passing that straight through erased an absent key and left the
// table's ramp climbing across the seek, so the first Skip after a seek pulled a
// full ceiling block instead of restarting at the floor -- defeating the exact
// "seek a record, read it, move on" case the ramp exists to protect. Found in
// code review; this pins it.
//
// Observable as a REQUEST count: after ramping to the ceiling and then seeking,
// a short forward scan needs several small (floor -> ramping) blocks if the seek
// reset the ramp, but is served in a single 64-row block if it did not.
TEST_CASE("remote ordered prefetch: a seek resets the depth ramp") {
    using openads::network::Server;
    const int N = 400;
    auto dir = fs::temp_directory_path() / "openads_ord_prefetch_seekramp";
    make_reversed_dbf(dir, N);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hTable = 0, hIndex = 0;
    open_ordered(hConn, &hTable, &hIndex);

    // Drive the ramp all the way to the ceiling first, so prefetch_depth_ holds
    // the table at 64. This is what makes the test discriminate: without the
    // fix the seek below leaves that 64 in place.
    REQUIRE(AdsGotoTop(hIndex) == 0);
    for (int i = 0; i < 200; ++i) REQUIRE(AdsSkip(hIndex, 1) == 0);

    // Seek to a LOW VAL -> near the top of the index order, so the scan below
    // has hundreds of rows of runway ahead of it (VAL 10 is index position 10).
    const double key = 10.0;
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex, (UNSIGNED8*)&key, sizeof(key),
                    ADS_DOUBLEKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found != 0);

    // A 60-row forward scan starting from a freshly-reset ramp needs several
    // refills (blocks of 8, 16, 32, 64 -> ~4 wire requests). If the ramp is
    // stuck at the ceiling, all 60 rows arrive in one 64-row block == 1 request.
    auto& stats = openads::mgmt::process_mg_stats();
    const std::uint64_t base = stats.packets_in.load();
    for (int i = 0; i < 60; ++i) REQUIRE(AdsSkip(hIndex, 1) == 0);
    const std::uint64_t reqs = stats.packets_in.load() - base;

    CHECK(reqs >= 3u);        // reset -> ~4; not reset (the bug) -> 1

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}
