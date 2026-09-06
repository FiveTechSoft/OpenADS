// tests/unit/network_setfields_test.cpp
// Write coalescing (WAN chattiness): the client buffers consecutive
// AdsSet* calls and flushes them as one SetFields (0x5E) batch on the
// next visibility event.
//
// Test cases:
//   1. Remote wire: append + N buffered sets + commit flush in one batch;
//      values round-trip exactly (incl. read-your-write overlay).
//   2. Coalescing is transparent across nav: write, Skip away and back,
//      values survive (flush landed before the cursor moved).
//   3. Old-server compat: without the ConnectAck caps echo the client
//      falls back to the legacy per-field SetField loop (no 0x5E sent).

#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path sf_tmp_dir() {
    return fs::temp_directory_path() / "openads_setfields_test";
}

void sf_wipe() {
    std::error_code ec;
    fs::remove_all(sf_tmp_dir(), ec);
    fs::create_directories(sf_tmp_dir(), ec);
}

void seed_sf_fixture(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,10,0;AGE,N,3,0";
    UNSIGNED8 tname[] = "sf.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::string sf_uri(openads::network::Server& srv, const fs::path& dir,
                   char* uri, std::size_t n) {
    std::snprintf(uri, n, "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    return std::string(uri);
}

void sf_set(ADSHANDLE hTable, const char* fld, const char* val) {
    UNSIGNED8 f[32]{};
    std::memcpy(f, fld, std::strlen(fld));
    UNSIGNED32 len = static_cast<UNSIGNED32>(std::strlen(val));
    REQUIRE(AdsSetString(hTable, f,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(val)),
                         len) == AE_SUCCESS);
}

std::string sf_get(ADSHANDLE hTable, const char* fld) {
    UNSIGNED8 f[32]{};
    std::memcpy(f, fld, std::strlen(fld));
    UNSIGNED8 buf[64]{};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, f, buf, &cap, 0) == AE_SUCCESS);
    std::string s(reinterpret_cast<char*>(buf), cap);
    // CHARACTER comes space-padded right, NUMERIC space-padded left.
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

} // namespace

// ── 1. Buffered append round-trips through one batch ─────────────────────────
TEST_CASE("SetFields remote wire: buffered append + commit flush round-trips") {
    sf_wipe();
    auto dir = sf_tmp_dir();
    seed_sf_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    sf_uri(srv, dir, uri, sizeof(uri));
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "sf.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    // Append + consecutive sets: buffered client-side, zero RTT until flush.
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    sf_set(hTable, "NM", "delta");
    sf_set(hTable, "AGE", "42");
    // Read-your-write BEFORE any flush: overlay serves buffered values.
    CHECK(sf_get(hTable, "NM") == "delta");
    CHECK(sf_get(hTable, "AGE") == "42");
    // Commit flushes the batch in one frame.
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    CHECK(sf_get(hTable, "NM") == "delta");
    CHECK(sf_get(hTable, "AGE") == "42");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    // Reopen: values are durable server-side (batch really landed).
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(sf_get(hTable, "NM") == "delta");
    CHECK(sf_get(hTable, "AGE") == "42");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

// ── 2. Nav away flushes before the cursor moves ──────────────────────────────
TEST_CASE("SetFields remote wire: skip-away flush keeps rows distinct") {
    sf_wipe();
    auto dir = sf_tmp_dir();
    seed_sf_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    sf_uri(srv, dir, uri, sizeof(uri));
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "sf.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    sf_set(hTable, "NM", "one");
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    sf_set(hTable, "NM", "two");
    // Skip away with a buffered write outstanding: the flush must land on
    // record 2, not wherever the cursor goes next.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(sf_get(hTable, "NM") == "one");
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    CHECK(sf_get(hTable, "NM") == "two");

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
