// network_exclusive_open_test.cpp -- mtfix11: ADS_EXCLUSIVE open
// enforcement across wire sessions.
//
// Root cause pinned here: the drivers map DriverOpenMode::Exclusive to a
// plain open (POSIX has no share modes), so an exclusive hold was a
// silent no-op - a second instance's opens sailed through a reindex/pack
// exclusive hold that SAP ADS would have denied, and the app's
// "only one user" startup abort never fired. The Server now keeps an
// open registry keyed by the engine's resolved path and denies
// conflicting opens with 7040 AE_FILE_IN_USE.
//
// Client park-pool interaction: our AdsCloseTable PARKS poolable shared
// tables (the server handle stays open for fast reuse), so a shared
// close does NOT release the server-side registration - the table IS
// still open on that session, and the registry correctly keeps blocking
// a peer's exclusive request. Exclusive tables are never parked, and a
// disconnect sweeps every registration the session still holds; those
// are the two release paths this test drives.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using openads::AE_SUCCESS;

namespace {

fs::path excl_tmp_dir() {
    return fs::temp_directory_path() / "openads_remote_excl_open";
}

// Recreates <tmp>/ex.dbf with 3 rows, NAME C(8).
void seed_excl_fixture(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto d = dir.string();
    std::memcpy(srv, d.c_str(), d.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tname[] = "ex.dbf";
    UNSIGNED8 def[]   = "NAME,C,8,0";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "NAME";
    for (int i = 0; i < 3; ++i) {
        char v[9];
        std::snprintf(v, sizeof(v), "R%07d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, fld, reinterpret_cast<UNSIGNED8*>(v),
                             8) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

UNSIGNED32 open_mode(ADSHANDLE hConn, const char* name, UNSIGNED16 mode,
                     ADSHANDLE* ph) {
    return AdsOpenTable(hConn,
                        reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                        nullptr, ADS_CDX, ADS_ANSI,
                        ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, mode, ph);
}

// Disconnect teardown is asynchronous with respect to another session's
// next request: the client returns from AdsDisconnect once its socket is
// down, while the server still has to observe the hangup and sweep that
// session's registrations. Poll briefly for the acquisition the sweep
// must unblock instead of assuming the sweep has already run.
UNSIGNED32 open_eventually(ADSHANDLE hConn, const char* name,
                           UNSIGNED16 mode, ADSHANDLE* ph) {
    UNSIGNED32 rc = AE_SUCCESS;
    for (int i = 0; i < 100; ++i) {
        rc = open_mode(hConn, name, mode, ph);
        if (rc == AE_SUCCESS) return rc;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return rc;
}

ADSHANDLE connect_remote(const UNSIGNED8* srvbuf) {
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(const_cast<UNSIGNED8*>(srvbuf), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) == AE_SUCCESS);
    return h;
}

} // namespace

TEST_CASE("mtfix11 exclusive open denies other sessions both directions") {
    auto dir = excl_tmp_dir();
    seed_excl_fixture(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    const char* tname = "ex.dbf";
    ADSHANDLE hA = connect_remote(srvbuf);
    ADSHANDLE hB = connect_remote(srvbuf);
    ADSHANDLE tA = 0, tA2 = 0, tB = 0;

    // 1. An exclusive holder blocks other sessions in both modes.
    REQUIRE(open_mode(hA, tname, ADS_EXCLUSIVE, &tA) == AE_SUCCESS);
    CHECK(open_mode(hB, tname, ADS_SHARED, &tB) != AE_SUCCESS);
    CHECK(tB == 0);
    CHECK(open_mode(hB, tname, ADS_EXCLUSIVE, &tB) != AE_SUCCESS);
    CHECK(tB == 0);

    // 2. The holding session itself may open the table again (several
    //    workareas on one connection are legitimate).
    REQUIRE(open_mode(hA, tname, ADS_SHARED, &tA2) == AE_SUCCESS);

    // 3. Closing the exclusive handle releases that hold (exclusive
    //    tables are never parked, so this close reaches the wire) - but
    //    the session's remaining shared open still blocks B's exclusive.
    REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
    CHECK(open_mode(hB, tname, ADS_EXCLUSIVE, &tB) != AE_SUCCESS);
    CHECK(tB == 0);

    // 4. Closing the shared handle parks it client-side; the following
    //    disconnect drains the park pool (a live open table would DEFER
    //    the disconnect instead) and the wire close frees the registry.
    REQUIRE(AdsCloseTable(tA2) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);
    REQUIRE(open_eventually(hB, tname, ADS_EXCLUSIVE, &tB) == AE_SUCCESS);

    // 5. Now B holds it exclusive: A's opens are denied.
    hA = connect_remote(srvbuf);
    tA = 0;
    CHECK(open_mode(hA, tname, ADS_SHARED, &tA) != AE_SUCCESS);
    CHECK(tA == 0);
    tA = 0;
    CHECK(open_mode(hA, tname, ADS_EXCLUSIVE, &tA) != AE_SUCCESS);
    CHECK(tA == 0);

    // 6. B's exclusive close is a real close: A gets in shared.
    REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
    REQUIRE(open_mode(hA, tname, ADS_SHARED, &tA) == AE_SUCCESS);

    // 7. Two shared holders coexist; a third session's exclusive fails.
    ADSHANDLE hC = connect_remote(srvbuf);
    ADSHANDLE tC = 0;
    REQUIRE(open_mode(hC, tname, ADS_SHARED, &tC) == AE_SUCCESS);
    ADSHANDLE hD = connect_remote(srvbuf);
    ADSHANDLE tD = 0;
    CHECK(open_mode(hD, tname, ADS_EXCLUSIVE, &tD) != AE_SUCCESS);
    CHECK(tD == 0);

    // 8. An upgrade to exclusive requires the table free of OTHER
    //    sessions: A still holds it shared, so C's exclusive is denied.
    ADSHANDLE tC2 = 0;
    CHECK(open_mode(hC, tname, ADS_EXCLUSIVE, &tC2) != AE_SUCCESS);
    CHECK(tC2 == 0);

    // 9. Once A lets go (parked close + disconnect drain), C's upgrade
    //    succeeds and D stays locked out.
    REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);
    REQUIRE(open_eventually(hC, tname, ADS_EXCLUSIVE, &tC2) == AE_SUCCESS);
    CHECK(open_mode(hD, tname, ADS_SHARED, &tD) != AE_SUCCESS);
    CHECK(tD == 0);

    // 10. C's exclusive close releases the exclusive hold; D may now
    //     share alongside C's still-open shared handle.
    REQUIRE(AdsCloseTable(tC2) == AE_SUCCESS);
    REQUIRE(open_mode(hD, tname, ADS_SHARED, &tD) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(tC) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(tD) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hD) == AE_SUCCESS);
}
