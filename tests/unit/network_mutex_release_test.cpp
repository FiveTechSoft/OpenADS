// tests/unit/network_mutex_release_test.cpp
// A dead session must not wedge a distributed mutex name.
// Field (v1.09.48): OAds_MutexCreate(cMutexName) failed until server
// restart after the creating process died — Session::cleanup released
// tables/indexes/files but never touched MutexManager (release_all was
// dead code, and it only unlocked without destroying anyway).
// The fix: create records the creator session; cleanup runs
// release_session (unlock-by-owner + destroy-by-creator, with
// creation transferred to a live lock holder instead of destroying
// under it). Both scenarios below failed before the fix.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

void mx_connect(openads::network::Server& s, const fs::path& dir,
                ADSHANDLE& hC) {
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
}

} // namespace

TEST_CASE("Mutex release: dead session's name is reusable") {
    auto dir = fs::temp_directory_path() / "openads_mutexrel";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());

    // Scenario 1: creator dies while HOLDING the lock (no unlock, no
    // destroy — the killed-process shape). A new session must be able
    // to create, lock, unlock and destroy the same name.
    {
        ADSHANDLE hC1 = 0;
        mx_connect(s, dir, hC1);
        UNSIGNED8 nm[] = "REL_HOLDER_DIES";
        REQUIRE(AdsMutexCreate(hC1, nm) == AE_SUCCESS);
        REQUIRE(AdsMutexLock(hC1, nm, 5000) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC1) == AE_SUCCESS);  // dies holding it

        ADSHANDLE hC2 = 0;
        mx_connect(s, dir, hC2);
        // Before the fix: AE failure ("exists") — server restart needed.
        REQUIRE(AdsMutexCreate(hC2, nm) == AE_SUCCESS);
        REQUIRE(AdsMutexLock(hC2, nm, 5000) == AE_SUCCESS);
        REQUIRE(AdsMutexUnlock(hC2, nm) == AE_SUCCESS);
        REQUIRE(AdsMutexDestroy(hC2, nm) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC2) == AE_SUCCESS);
    }

    // Scenario 2: creator dies while a PEER holds the lock (handover).
    // The peer must keep working; the name must die with the peer.
    {
        ADSHANDLE hC1 = 0, hC2 = 0;
        mx_connect(s, dir, hC1);
        mx_connect(s, dir, hC2);
        UNSIGNED8 nm[] = "REL_HANDOVER";
        REQUIRE(AdsMutexCreate(hC1, nm) == AE_SUCCESS);
        REQUIRE(AdsMutexLock(hC2, nm, 5000) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC1) == AE_SUCCESS);  // creator dies first
        // Peer still owns a live mutex: unlock works, name survives.
        REQUIRE(AdsMutexUnlock(hC2, nm) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC2) == AE_SUCCESS);  // last holder dies

        ADSHANDLE hC3 = 0;
        mx_connect(s, dir, hC3);
        REQUIRE(AdsMutexCreate(hC3, nm) == AE_SUCCESS);
        REQUIRE(AdsMutexDestroy(hC3, nm) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC3) == AE_SUCCESS);
    }

    s.stop();
    fs::remove_all(dir, ec);
}

