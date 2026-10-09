#include "doctest.h"
#include "openads/ace.h"
#include "drivers/cdx/cdx_driver.h"
#include "network/client.h"
#include "network/server.h"

#include <array>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::cdx::CdxDriver;
using openads::drivers::DriverOpenMode;

// Regression — ADSCDX error 5000 "record number out of range" during a
// multiuser REPLACE ... FOR (DBEVAL).
//
// A CdxDriver caches the DBF record count at open() time. When a peer
// connection appends a record afterwards (the normal multiuser ERP case:
// one station runs a batch REPLACE while another inserts), the first
// connection's cached count lags the on-disk truth. An index walk then
// hands read_record_raw()/write_record_raw() a recno that is valid on
// disk but beyond the stale cache, and the driver used to fail it hard
// with 5000 mid-scan — the exact symptom a user hit on CONSEINV.
//
// Native ADSCDX tolerates this (it re-reads the header). The fetch path
// now re-reads the on-disk count (under a shared header lock) before
// declaring a recno out of range, so a peer's freshly appended rows
// become reachable instead of crashing the scan. The slow path only runs
// when recno > cached count, so a normal forward scan pays nothing.
TEST_CASE("CdxDriver fetch sees a peer's appended record (no spurious 5000)") {
    auto dir = fs::temp_directory_path() / "openads_peer_append_5000";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    // Create "inv" with 3 records via the ABI, then close so the .dbf is a
    // plain file on disk that both drivers can open.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 def[]   = "ID,N,6,0";
        UNSIGNED8 tname[] = "inv";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        for (int i = 0; i < 3; ++i) {
            REQUIRE(AdsAppendRecord(hT) == 0);
        }
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    const auto dbf = (dir / "inv.dbf").string();

    // Connection A opens and caches rec_count_ == 3.
    CdxDriver a;
    REQUIRE(a.open(dbf, DriverOpenMode::Shared).has_value());
    REQUIRE(a.record_count() == 3u);

    // Connection B (a peer) appends a 4th record on disk.
    {
        CdxDriver b;
        REQUIRE(b.open(dbf, DriverOpenMode::Shared).has_value());
        std::vector<std::uint8_t> rec(b.record_length(), 0x20); // spaces
        rec[0] = 0x20;                                           // not deleted
        auto ap = b.append_record_raw(rec.data(), rec.size());
        REQUIRE(ap.has_value());
        CHECK(ap.value() == 4u);
    }

    // A's cache still says 3, but recno 4 physically exists. Before the
    // fix read_record_raw(4) returns 5000; after it refreshes and reads.
    auto got = a.read_record_raw(4u);
    CHECK_MESSAGE(got.has_value(),
        "peer-appended recno 4 must be readable, not ADSCDX 5000");

    // The write path a REPLACE takes must also reach the row.
    if (got.has_value()) {
        auto wr = a.write_record_raw(4u, got.value().data(),
                                     got.value().size());
        CHECK_MESSAGE(wr.has_value(),
            "writing peer-appended recno 4 must not raise ADSCDX 5000");
    }

    fs::remove_all(dir, ec);
}

// Focused physical-count regressions. Direct wire requests intentionally avoid
// changing the separate ACE client count-cache policy.
namespace {
void count_seed(const fs::path& p, std::uint32_t header_count = 3) {
    std::array<std::uint8_t, 65> h{};
    h[0] = 3;
    h[4] = static_cast<std::uint8_t>(header_count);
    h[8] = 65;
    h[10] = 6;
    h[32] = 'V'; h[43] = 'C'; h[48] = 5; h[64] = 0x0D;
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(h.data()), h.size());
    for (int n = 0; n < 3; ++n) f.write(" AAAAA", 6);
    f.put(0x1A);
}
void count_header(const fs::path& p, std::uint32_t n) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    std::array<std::uint8_t, 4> b{{static_cast<std::uint8_t>(n),
        static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n >> 16),
        static_cast<std::uint8_t>(n >> 24)}};
    f.seekp(4); f.write(reinterpret_cast<const char*>(b.data()), b.size());
}
fs::path count_dir(const char* name) {
    auto d = fs::temp_directory_path() / name;
    std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d);
    return d;
}
}

TEST_CASE("Physical count: ordinary DBF refresh follows body size, not stale header") {
    const auto dir = count_dir("openads_physical_count_body");
    const auto p = dir / "data.dbf";
    count_seed(p);
    {
        CdxDriver driver;
        REQUIRE(driver.open(p.string(), DriverOpenMode::Shared).has_value());
        count_header(p, 99);
        REQUIRE(driver.refresh_record_count_from_disk().has_value());
        CHECK(driver.record_count() == 3);
        count_header(p, 1);
        REQUIRE(driver.refresh_record_count_from_disk().has_value());
        CHECK(driver.record_count() == 3);
        // A peer's shrink with a stale high header must not resurrect rows.
        count_header(p, 99);
        fs::resize_file(p, 65 + 6);
        REQUIRE(driver.refresh_record_count_from_disk().has_value());
        CHECK(driver.record_count() == 1);
        fs::resize_file(p, 65);
        REQUIRE(driver.refresh_record_count_from_disk().has_value());
        CHECK(driver.record_count() == 0);
        fs::resize_file(p, 65 + 1); // optional EOF byte, no records
        REQUIRE(driver.refresh_record_count_from_disk().has_value());
        CHECK(driver.record_count() == 0);
        fs::resize_file(p, 64); // enough bytes to read count, short DBF header
        auto short_body = driver.refresh_record_count_from_disk();
        REQUIRE_FALSE(short_body.has_value());
        CHECK(short_body.error().code == 5103);
        fs::resize_file(p, 4);
        auto short_header = driver.refresh_record_count_from_disk();
        REQUIRE_FALSE(short_header.has_value());
        CHECK(short_header.error().code == 5103);
    }
    fs::remove_all(dir);
}

TEST_CASE("Physical count: local ACE returns refresh errors without outputting stale count") {
    const auto dir = count_dir("openads_physical_count_ace");
    const auto p = dir / "data.dbf";
    count_seed(p);
    auto path = dir.string();
    ADSHANDLE connection = 0, table = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(path.data()),
        ADS_LOCAL_SERVER, nullptr, nullptr, 0, &connection) == AE_SUCCESS);
    UNSIGNED8 name[] = "data.dbf";
    REQUIRE(AdsOpenTable(connection, name, nullptr, ADS_CDX, ADS_ANSI,
        ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, ADS_SHARED, &table) == AE_SUCCESS);
    count_header(p, 99);
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(table, ADS_IGNOREFILTERS, &count) == AE_SUCCESS);
    CHECK(count == 3);
    fs::resize_file(p, 4);
    count = 0xDEADBEEFu;
    CHECK(AdsGetRecordCount(table, ADS_IGNOREFILTERS, &count) == 5103);
    CHECK(count == 0xDEADBEEFu);
    CHECK(AdsCloseTable(table) == AE_SUCCESS);
    CHECK(AdsDisconnect(connection) == AE_SUCCESS);
    fs::remove_all(dir);
}

TEST_CASE("Physical count: direct remote refresh sees peer append and forwards disk errors") {
    const auto dir = count_dir("openads_physical_count_wire");
    const auto p = dir / "data.dbf";
    count_seed(p);
    openads::network::Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    openads::network::RemoteConnection client;
    REQUIRE(client.connect("127.0.0.1", server.port(), dir.string()).has_value());
    auto opened = client.open_table("data.dbf");
    REQUIRE(opened.has_value());
    const auto id = opened.value().id;
    REQUIRE(client.goto_top(id).has_value());
    auto count = client.record_count(id);
    REQUIRE(count.has_value()); CHECK(count.value() == 3);
    {
        CdxDriver peer;
        REQUIRE(peer.open(p.string(), DriverOpenMode::Shared).has_value());
        std::vector<std::uint8_t> row(peer.record_length(), ' ');
        REQUIRE(peer.append_record_raw(row.data(), row.size()).has_value());
    }
    // Make the header lag behind the new physical row, then overstate it.
    count_header(p, 3);
    count = client.record_count(id);
    REQUIRE(count.has_value()); CHECK(count.value() == 4);
    count_header(p, 99);
    count = client.record_count(id);
    REQUIRE(count.has_value()); CHECK(count.value() == 4);
    fs::resize_file(p, 65); // peer emptied the body without updating header
    count = client.record_count(id);
    REQUIRE(count.has_value()); CHECK(count.value() == 0);
    fs::resize_file(p, 4);
    count = client.record_count(id);
    REQUIRE_FALSE(count.has_value()); CHECK(count.error().code == 5103);
    CHECK(client.close_table(id).has_value());
    client.disconnect(); server.stop();
    fs::remove_all(dir);
}
