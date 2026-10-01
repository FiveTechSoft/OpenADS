// tests/unit/network_lock_exclusion_test.cpp
// Record-lock exclusion across sessions (single-login enforcement).
// Field: process A locks its username record for life; occasionally a
// second process B locks the SAME record and proceeds (never on
// DBFCDX). The gate is LockRecord -> server try_lock_record_excl ->
// OS byte lock. IsRecordLocked was own-table-only at birth (the
// single-login guard that QUERIED instead of attempting stayed blind);
// mtfix11 makes it cross-owner aware via the OS probe, matching SAP -
// but the enforcement gate below still rests on LockRecord attempts.
//
// Matrix: A holds rec 3 (natural vs ordered/twin path on each side),
// B must fail the same recno. Then close/reopen handover: A closes
// (lock dropped by design), B locks, A reopens and must fail.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

void lx_connect(openads::network::Server& s, const fs::path& dir,
                ADSHANDLE& hC) {
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
}

void lx_open(ADSHANDLE hC, const char* tn, ADSHANDLE& hT) {
    UNSIGNED8 tnu[64]{};
    std::memcpy(tnu, tn, std::strlen(tn) + 1);
    REQUIRE(AdsOpenTable(hC, tnu, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
}

// Activate the BYNAME order (forces the server ABI-twin path for
// subsequent seeks/locks) and seek the username. Returns the recno.
std::uint32_t lx_seek_name(ADSHANDLE hT, const char* user) {
    UNSIGNED8 want[] = "BYNAME";
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);
    UNSIGNED16 found = 0;
    std::size_t kl = std::strlen(user);
    UNSIGNED8 kb[64]{};
    std::memcpy(kb, user, kl);
    REQUIRE(AdsSeek(hOrd, kb, static_cast<UNSIGNED16>(kl), ADS_STRINGKEY,
                    ADS_SOFTSEEK, &found) == AE_SUCCESS);
    REQUIRE(found == 1);
    UNSIGNED32 recno = 0;
    UNSIGNED16 f = 0;
    REQUIRE(AdsGetRecordNum(hT, f, &recno) == AE_SUCCESS);
    return recno;
}

} // namespace

TEST_CASE("Lock exclusion: second session cannot take a held record") {
    auto dir = fs::temp_directory_path() / "openads_lockexcl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Fixture: USERS.DBF with 5 rows, index BYNAME on USERNAME.
    {
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC0 = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
                == AE_SUCCESS);
        UNSIGNED8 def[] = "USERNAME,C,20,0;ID,N,8,0";
        UNSIGNED8 tn0[] = "USERS.DBF";
        ADSHANDLE hT0 = 0;
        REQUIRE(AdsCreateTable(hC0, tn0, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                               &hT0) == AE_SUCCESS);
        UNSIGNED8 fu[] = "USERNAME", fi[] = "ID";
        const char* names[] = {"ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO"};
        for (int i = 0; i < 5; ++i) {
            REQUIRE(AdsAppendRecord(hT0) == AE_SUCCESS);
            REQUIRE(AdsSetString(hT0, fu, (UNSIGNED8*)names[i],
                                 (UNSIGNED32)std::strlen(names[i]))
                    == AE_SUCCESS);
            REQUIRE(AdsSetDouble(hT0, fi, 100 + i) == AE_SUCCESS);
            REQUIRE(AdsWriteRecord(hT0) == AE_SUCCESS);
        }
        ADSHANDLE hI0 = 0;
        UNSIGNED8 bag[] = "USERS.CDX", tag[] = "BYNAME", exp[] = "USERNAME";
        REQUIRE(AdsCreateIndex61(hT0, bag, tag, exp, nullptr, nullptr,
                                 ADS_COMPOUND, 512, &hI0) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);
    }

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());

    // use_ordered: false = natural/engine path, true = twin path (seek).
    auto round = [&](bool a_ordered, bool b_ordered) {
        ADSHANDLE hA = 0, hB = 0;
        lx_connect(s, dir, hA);
        lx_connect(s, dir, hB);
        ADSHANDLE tA = 0, tB = 0;
        lx_open(hA, "USERS.DBF", tA);
        lx_open(hB, "USERS.DBF", tB);

        std::uint32_t recA =
            a_ordered ? lx_seek_name(tA, "CHARLIE") : 3u;
        if (!a_ordered) {
            REQUIRE(AdsGotoRecord(tA, 3) == AE_SUCCESS);
        }
        REQUIRE(AdsLockRecord(tA, recA) == AE_SUCCESS);  // held for life

        std::uint32_t recB =
            b_ordered ? lx_seek_name(tB, "CHARLIE") : 3u;
        if (!b_ordered) {
            REQUIRE(AdsGotoRecord(tB, 3) == AE_SUCCESS);
        }
        // Same physical row on both sides?
        CHECK(recB == recA);

        // The gate: B must NOT get it.
        UNSIGNED16 bLocked = 0;
        REQUIRE(AdsIsRecordLocked(tB, recB, &bLocked) == AE_SUCCESS);
        CHECK(AdsLockRecord(tB, recB) != AE_SUCCESS);

        REQUIRE(AdsUnlockRecord(tA, recA) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    };

    round(false, false);
    round(false, true);
    round(true, false);
    round(true, true);

    // Handover: A closes (lock dropped by design — same as DBFCDX),
    // B takes it, A reopens and must fail.
    {
        ADSHANDLE hA = 0, hB = 0;
        lx_connect(s, dir, hA);
        lx_connect(s, dir, hB);
        ADSHANDLE tA = 0, tB = 0;
        lx_open(hA, "USERS.DBF", tA);
        REQUIRE(AdsLockRecord(tA, 3) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);  // lock released here

        lx_open(hB, "USERS.DBF", tB);
        REQUIRE(AdsLockRecord(tB, 3) == AE_SUCCESS);  // B takes it

        lx_open(hA, "USERS.DBF", tA);
        CHECK(AdsLockRecord(tA, 3) != AE_SUCCESS);  // A must fail now

        REQUIRE(AdsUnlockRecord(tB, 3) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    }

    s.stop();
    fs::remove_all(dir, ec);
}
