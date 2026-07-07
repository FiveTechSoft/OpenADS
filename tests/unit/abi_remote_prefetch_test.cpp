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
