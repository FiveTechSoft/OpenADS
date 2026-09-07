// tests/unit/network_open_warm_test.cpp
// Warm OpenTableAck (USE latency): schema + first row ride the open
// reply so a remote USE costs OpenTable + production auto-open only —
// no DescribeTable, no implicit GotoTop round-trip.
//
// Proof strategy: the in-process server accumulates per-opcode counters
// in process_mg_stats(). Snapshot them around AdsOpenTable and assert
// DescribeTable (0x4A) and GotoTop (0x40) did NOT hit the wire, then
// exercise the warmed state (immediate field read, two prefetched
// skips with no further Skip frames).

#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path ow_tmp_dir() {
    return fs::temp_directory_path() / "openads_openwarm_test";
}

void ow_wipe() {
    std::error_code ec;
    fs::remove_all(ow_tmp_dir(), ec);
    fs::create_directories(ow_tmp_dir(), ec);
}

void seed_ow_fixture(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,10,0";
    UNSIGNED8 tname[] = "ow.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "NM";
    const char* vals[] = {"alpha", "bravo", "charlie"};
    for (const char* v : vals) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        UNSIGNED32 len = static_cast<UNSIGNED32>(std::strlen(v));
        REQUIRE(AdsSetString(hTable, fld,
                             reinterpret_cast<UNSIGNED8*>(const_cast<char*>(v)),
                             len) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::uint64_t op_count(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op].count.load(std::memory_order_relaxed);
}

std::string ow_get(ADSHANDLE hTable, const char* fld) {
    UNSIGNED8 f[32]{};
    std::memcpy(f, fld, std::strlen(fld));
    UNSIGNED8 buf[64]{};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, f, buf, &cap, 0) == AE_SUCCESS);
    std::string s(reinterpret_cast<char*>(buf), cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

TEST_CASE("Warm open: schema + first row ride the OpenTableAck") {
    ow_wipe();
    auto dir = ow_tmp_dir();
    seed_ow_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    const std::uint64_t desc_before = op_count(0x4A);  // DescribeTable
    const std::uint64_t top_before  = op_count(0x40);  // GotoTop

    UNSIGNED8 tname[] = "ow.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    // The open carried schema + first row: neither opcode hit the wire.
    // (Auto-open OpenIndex attempts for the missing bag still do — the
    // fixture has no .cdx — but those predate sections.)
    CHECK(op_count(0x4A) == desc_before);
    CHECK(op_count(0x40) == top_before);

    // Warmed state is immediately usable: first-row read with no fetch,
    // then two skips served from the rode-along lookahead block.
    CHECK(ow_get(hTable, "NM") == "alpha");
    const std::uint64_t skip_before = op_count(0x42);  // Skip
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    CHECK(ow_get(hTable, "NM") == "bravo");
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    CHECK(ow_get(hTable, "NM") == "charlie");
    CHECK(op_count(0x42) == skip_before);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Pooled re-USE: close/reopen skips OpenTable+CloseTable frames") {
    ow_wipe();
    auto dir = ow_tmp_dir();
    seed_ow_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "ow.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    const std::uint64_t open_before  = op_count(0x20);  // OpenTable
    const std::uint64_t close_before = op_count(0x22);  // CloseTable
    const std::uint64_t top_before   = op_count(0x40);  // GotoTop

    // Re-USE within TTL: parked server handle is adopted, so neither
    // OpenTable nor the first CloseTable reached the wire. Exactly one
    // warm GotoTop repositions.
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    CHECK(op_count(0x20) == open_before);
    CHECK(ow_get(hTable, "NM") == "alpha");
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    CHECK(op_count(0x22) == close_before);
    CHECK(op_count(0x40) == top_before + 1);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Pooled re-USE skipped after a lock (real close)") {    ow_wipe();
    auto dir = ow_tmp_dir();
    seed_ow_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "ow.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hTable, 0) == AE_SUCCESS);
    REQUIRE(AdsUnlockRecord(hTable, 0) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Locked once: pool-ineligible, so the reopen is a full wire open.
    const std::uint64_t open_before = op_count(0x20);
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    CHECK(op_count(0x20) == open_before + 1);
    CHECK(ow_get(hTable, "NM") == "alpha");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("DropTable after pooled close really removes the file") {
    ow_wipe();
    auto dir = ow_tmp_dir();
    seed_ow_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Open + close parks the handle (shared, natural, never locked).
    UNSIGNED8 tname[] = "ow.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    {
        std::error_code ec;
        CHECK(fs::exists(dir / "ow.dbf"));
        (void)ec;
    }
    // Drop must evict the parked handle first: the file really goes.
    REQUIRE(AdsDropTable(hConn, tname, 1) == AE_SUCCESS);
    {
        std::error_code ec;
        CHECK_FALSE(fs::exists(dir / "ow.dbf", ec));
    }

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
