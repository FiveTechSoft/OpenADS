// tests/unit/network_login_gate_stress_test.cpp
// Exact Vouch single-login gate shape under churn:
//   IF DBSeek(cUser) -> IF !DbrLock(RecNo()) -> deny
// A holds CHARLIE's record; B must never take it — across repeated
// open/seek/lock cycles, a racing second thread (pool lane 1), and a
// third session appending/deleting sibling rows (recno churn).
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace {

void g_connect(openads::network::Server& s, const fs::path& dir,
               ADSHANDLE& hC) {
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
}

void g_open(ADSHANDLE hC, const char* tn, ADSHANDLE& hT) {
    UNSIGNED8 tnu[64]{};
    std::memcpy(tnu, tn, std::strlen(tn) + 1);
    REQUIRE(AdsOpenTable(hC, tnu, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
}

// The gate: seek user on BYNAME, lock RecNo(). Returns lock rc.
UNSIGNED32 gate_try(ADSHANDLE hT, const char* user, UNSIGNED32& recno,
                    UNSIGNED16& found) {    UNSIGNED8 want[] = "BYNAME";
    ADSHANDLE hOrd = 0;
    if (AdsGetIndexHandle(hT, want, &hOrd) != AE_SUCCESS) return 999998;
    if (AdsSetIndexOrderByHandle(hT, hOrd) != AE_SUCCESS) return 999997;
    std::size_t kl = std::strlen(user);
    UNSIGNED8 kb[64]{};
    std::memcpy(kb, user, kl);
    found = 0;
    if (AdsSeek(hOrd, kb, static_cast<UNSIGNED16>(kl), ADS_STRINGKEY,
                ADS_SOFTSEEK, &found) != AE_SUCCESS)
        return 999996;
    if (!found) return 999995;  // gate: DBSeek false -> allow (no lock)
    recno = 0;
    UNSIGNED16 f = 0;
    if (AdsGetRecordNum(hT, f, &recno) != AE_SUCCESS) return 999994;
    return AdsLockRecord(hT, recno);
}

} // namespace

TEST_CASE("Login gate: holder blocks repeated challengers + churn") {
    // Fast contention: single attempt, no ~1s retry budget per try.
    AdsSetLockRetryCount(0, 0);
    AdsSetLockCycle(0, 1);

    auto dir = fs::temp_directory_path() / "openads_logingate";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

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

    // A logs in first and holds for the whole test.
    ADSHANDLE hA = 0;
    g_connect(s, dir, hA);
    ADSHANDLE tA = 0;
    g_open(hA, "USERS.DBF", tA);
    UNSIGNED32 recA = 0;
    UNSIGNED16 foundA = 0;
    REQUIRE(gate_try(tA, "CHARLIE", recA, foundA) == AE_SUCCESS);
    REQUIRE(foundA == 1);
    CHECK(recA == 3);

    // B challenges 25x with fresh opens (fresh twin + index each time).
    for (int i = 0; i < 25; ++i) {
        ADSHANDLE hB = 0;
        g_connect(s, dir, hB);
        ADSHANDLE tB = 0;
        g_open(hB, "USERS.DBF", tB);
        UNSIGNED32 recB = 0;
        UNSIGNED16 foundB = 0;
        UNSIGNED32 rc = gate_try(tB, "CHARLIE", recB, foundB);
        CHECK(foundB == 1);
        CHECK(recB == recA);
        CHECK(rc != AE_SUCCESS);  // must be denied
        if (rc == AE_SUCCESS) {
            // Do not leave a stolen lock behind.
            (void)AdsUnlockRecord(tB, recB);
        }
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    }

    // Racing challenger on a second thread (pool lane 1 on its own
    // connection), while a third session churns sibling rows.
    std::atomic<int> steals{0};
    std::atomic<bool> stop{false};
    std::thread churn([&] {
        ADSHANDLE hC = 0;
        g_connect(s, dir, hC);
        ADSHANDLE tC = 0;
        g_open(hC, "USERS.DBF", tC);
        UNSIGNED8 fu[] = "USERNAME", fi[] = "ID";
        int n = 0;
        while (!stop.load()) {
            char nm[16]{};
            std::snprintf(nm, sizeof(nm), "TMP%04d", n++);
            (void)AdsAppendRecord(tC);
            (void)AdsSetString(tC, fu, (UNSIGNED8*)nm,
                               (UNSIGNED32)std::strlen(nm));
            (void)AdsSetDouble(tC, fi, 900 + n);
            (void)AdsWriteRecord(tC);
            if (n % 4 == 0) {
                // Delete it back: recno churn around the held row.
                UNSIGNED32 last = 0;
                UNSIGNED16 f = 0;
                if (AdsGetRecordCount(tC, f, &last) == AE_SUCCESS && last > 5) {
                    if (AdsGotoRecord(tC, last) == AE_SUCCESS)
                        (void)AdsDeleteRecord(tC);
                }
            }
        }
        (void)AdsCloseTable(tC);
        (void)AdsDisconnect(hC);
    });
    std::thread racer([&] {
        for (int i = 0; i < 25 && !stop.load(); ++i) {
            ADSHANDLE hB = 0;
            g_connect(s, dir, hB);
            ADSHANDLE tB = 0;
            g_open(hB, "USERS.DBF", tB);
            UNSIGNED32 recB = 0;
            UNSIGNED16 foundB = 0;
            UNSIGNED32 rc = gate_try(tB, "CHARLIE", recB, foundB);
            if (foundB == 1 && recB == recA && rc == AE_SUCCESS) {
                steals.fetch_add(1);
                (void)AdsUnlockRecord(tB, recB);
            }
            (void)AdsCloseTable(tB);
            (void)AdsDisconnect(hB);
        }
    });
    racer.join();
    stop.store(true);
    churn.join();
    CHECK(steals.load() == 0);

    // A still holds; release cleanly.
    REQUIRE(AdsUnlockRecord(tA, recA) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);

    s.stop();
    fs::remove_all(dir, ec);
}

// Literal field snippet: probe-lock cOther, UNLOCK it, re-seek cUser,
// hold-lock. lRet=true means "login granted".
bool field_gate(ADSHANDLE hT, const char* cOther, const char* cUser) {
    bool lRet = true;
    UNSIGNED32 rec = 0;
    UNSIGNED16 found = 0;
    if (gate_try(hT, cOther, rec, found) == AE_SUCCESS && found == 1) {
        // probe took it: release and continue
        if (AdsUnlockRecord(hT, rec) != AE_SUCCESS) return false;
    } else if (found == 1) {
        return false;  // Alert O: cOther held elsewhere
    }
    if (lRet) {
        if (gate_try(hT, cUser, rec, found) == AE_SUCCESS && found == 1) {
            return true;  // hold-lock acquired
        }
        if (found == 1) return false;  // Alert U: cUser held elsewhere
    }
    return lRet;
}

TEST_CASE("Login gate: probe unlock + reseek + hold never steals") {
    AdsSetLockRetryCount(0, 0);
    AdsSetLockCycle(0, 1);

    auto dir = fs::temp_directory_path() / "openads_logingate2";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

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

    // A logs in via the same snippet and holds CHARLIE.
    ADSHANDLE hA = 0;
    g_connect(s, dir, hA);
    ADSHANDLE tA = 0;
    g_open(hA, "USERS.DBF", tA);
    REQUIRE(field_gate(tA, "BRAVO", "CHARLIE") == true);

    // B runs the full snippet 30x: fresh table each time (fresh twin),
    // plus 30x reusing ONE kept-open table (stale twin/caches).
    for (int i = 0; i < 30; ++i) {
        ADSHANDLE hB = 0;
        g_connect(s, dir, hB);
        ADSHANDLE tB = 0;
        g_open(hB, "USERS.DBF", tB);
        CHECK(field_gate(tB, "BRAVO", "CHARLIE") == false);
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    }
    {
        ADSHANDLE hB = 0;
        g_connect(s, dir, hB);
        ADSHANDLE tB = 0;
        g_open(hB, "USERS.DBF", tB);
        for (int i = 0; i < 30; ++i) {
            INFO("reuse iter " << i);
            CHECK(field_gate(tB, "BRAVO", "CHARLIE") == false);
        }
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    }

    // A releases; the same snippet must now grant.
    {
        UNSIGNED32 recA = 3;
        REQUIRE(AdsUnlockRecord(tA, recA) == AE_SUCCESS);
    }
    {
        ADSHANDLE hB = 0;
        g_connect(s, dir, hB);
        ADSHANDLE tB = 0;
        g_open(hB, "USERS.DBF", tB);
        CHECK(field_gate(tB, "BRAVO", "CHARLIE") == true);
        if (true) {
            // hold-lock acquired above; find + release it.
            UNSIGNED32 rec = 0;
            UNSIGNED16 f = 0, found = 0;
            UNSIGNED8 want[] = "BYNAME";
            ADSHANDLE hOrd = 0;
            REQUIRE(AdsGetIndexHandle(tB, want, &hOrd) == AE_SUCCESS);
            REQUIRE(AdsSetIndexOrderByHandle(tB, hOrd) == AE_SUCCESS);
            UNSIGNED8 kb[] = "CHARLIE";
            REQUIRE(AdsSeek(hOrd, kb, 7, ADS_STRINGKEY, ADS_SOFTSEEK, &found)
                    == AE_SUCCESS);
            REQUIRE(found == 1);
            REQUIRE(AdsGetRecordNum(tB, f, &rec) == AE_SUCCESS);
            REQUIRE(AdsUnlockRecord(tB, rec) == AE_SUCCESS);
        }
        REQUIRE(AdsCloseTable(tB) == AE_SUCCESS);
        REQUIRE(AdsDisconnect(hB) == AE_SUCCESS);
    }

    REQUIRE(AdsCloseTable(tA) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hA) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Login gate: simultaneous racers, exactly one holder") {
    AdsSetLockRetryCount(0, 0);
    AdsSetLockCycle(0, 1);

    auto dir = fs::temp_directory_path() / "openads_logingate3";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

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

    // N racers start the hold-lock at the same instant and KEEP it until
    // all are done. Exactly one may hold CHARLIE at once.
    constexpr int N = 8;
    std::atomic<int> ready{0};
    std::atomic<int> holders{0};
    std::atomic<int> max_holders{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> th;
    for (int t = 0; t < N; ++t) {
        th.emplace_back([&] {
            ADSHANDLE hB = 0;
            g_connect(s, dir, hB);
            ADSHANDLE tB = 0;
            g_open(hB, "USERS.DBF", tB);
            ready.fetch_add(1);
            while (!go.load()) std::this_thread::yield();
            UNSIGNED32 recB = 0;
            UNSIGNED16 foundB = 0;
            if (gate_try(tB, "CHARLIE", recB, foundB) == AE_SUCCESS
                && foundB == 1) {
                int cur = holders.fetch_add(1) + 1;
                int prev = max_holders.load();
                while (cur > prev
                       && !max_holders.compare_exchange_weak(prev, cur)) {}
                // Hold it a beat so overlap is certain.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                holders.fetch_sub(1);
                (void)AdsUnlockRecord(tB, recB);
            }
            (void)AdsCloseTable(tB);
            (void)AdsDisconnect(hB);
        });
    }
    while (ready.load() < N) std::this_thread::yield();
    go.store(true);
    for (auto& t : th) t.join();
    CHECK(max_holders.load() == 1);

    s.stop();
    fs::remove_all(dir, ec);
}
