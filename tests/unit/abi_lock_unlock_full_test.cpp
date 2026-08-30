// Comprehensive lock/unlock coverage (Pritpal Bedi: "dbUnlock() in
// threads fail somehow", Aug 2026). Root cause found and fixed: an
// AI-session commit (bb86a40) had flipped AdsUnlockTable to release only
// the table lock, leaking record locks; the SAP-verified semantic
// (commit 1fb224b) is that AdsUnlockTable releases EVERYTHING — table
// lock and every record lock. Harbour's rddads maps dbUnlock() straight
// to AdsUnlockTable with no client-side lock list, so any leak there
// blocks the next dbRLock() forever (the GN_COUNT R-LOCKING loop).
//
// This file pins every lock/unlock case: current vs explicit recno,
// refcounted double locks, unlock of non-held records, append auto-lock
// release, the dbUnlock equivalence, cross-connection contention, and
// the multi-thread shared-connection scenario from the field.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_local(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    UNSIGNED8 srv[512] = {};
    const auto s = dir.string();
    std::memcpy(srv, s.c_str(), s.size() + 1);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE connect_remote(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    ADSHANDLE h = 0;
    std::vector<UNSIGNED8> u(uri.c_str(), uri.c_str() + uri.size() + 1);
    REQUIRE(AdsConnect60(u.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) == 0);
    return h;
}

void stage_table(const fs::path& dir, const char* name, int nrecs,
                 ADSHANDLE hConn) {
    (void)dir;
    UNSIGNED8 tname[64] = {};
    std::memcpy(tname, name, std::strlen(name) + 1);  // NOLINT
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           ADS_CHECKRIGHTS, ADS_DEFAULT, 0,
                           (UNSIGNED8*)"VAL,N,8,0", &hT) == 0);
    for (int i = 0; i < nrecs; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetLong(hT, (UNSIGNED8*)"VAL", i);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
}

ADSHANDLE open_shared(ADSHANDLE hConn, const char* name) {
    UNSIGNED8 tname[64] = {};
    std::memcpy(tname, name, std::strlen(name) + 1);  // NOLINT
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_CDX, 1, 1, 0, 1, &hT) == 0);
    return hT;
}

UNSIGNED32 num_locks(ADSHANDLE hT) {
    UNSIGNED16 n = 0xFFFF;
    REQUIRE(AdsGetNumLocks(hT, &n) == 0);
    return n;
}

}  // namespace

// ===========================================================================
// LOCAL
// ===========================================================================
TEST_CASE("lockfull local: lock current then unlock current") {
    auto dir = fs::temp_directory_path() / "oads_lf_current";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC1", 5, hC);
    auto hT = open_shared(hC, "LC1");
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsLockRecord(hT, 0) == 0);   // 0 = current
    CHECK(num_locks(hT) == 1u);
    REQUIRE(AdsUnlockRecord(hT, 0) == 0);
    CHECK(num_locks(hT) == 0u);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull local: double lock is refcounted, needs two unlocks") {
    auto dir = fs::temp_directory_path() / "oads_lf_refcount";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC2", 5, hC);
    auto hT = open_shared(hC, "LC2");
    REQUIRE(AdsLockRecord(hT, 2) == 0);
    REQUIRE(AdsLockRecord(hT, 2) == 0);
    CHECK(num_locks(hT) == 1u);
    REQUIRE(AdsUnlockRecord(hT, 2) == 0);
    REQUIRE(AdsUnlockRecord(hT, 2) == 0);
    CHECK(num_locks(hT) == 0u);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull local: unlock of a non-held record is a safe no-op") {
    auto dir = fs::temp_directory_path() / "oads_lf_notheld";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC3", 5, hC);
    auto hT = open_shared(hC, "LC3");
    // Idempotent by design: unlocking a record we don't hold must not
    // error out (apps double-unlock on error paths all the time).
    CHECK(AdsUnlockRecord(hT, 4) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull local: UnlockTable with no table lock releases RLocks") {
    // The Pritpal case: dbRLock() then dbUnlock() — no FLock involved.
    auto dir = fs::temp_directory_path() / "oads_lf_dbunlock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC4", 5, hC);
    auto hT = open_shared(hC, "LC4");
    REQUIRE(AdsLockRecord(hT, 1) == 0);
    REQUIRE(AdsLockRecord(hT, 3) == 0);
    CHECK(num_locks(hT) == 2u);
    REQUIRE(AdsUnlockTable(hT) == 0);   // dbUnlock()
    CHECK(num_locks(hT) == 0u);         // SAP: everything released
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull local: append auto-lock is released by UnlockTable") {
    auto dir = fs::temp_directory_path() / "oads_lf_appendlock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC5", 3, hC);
    auto hT = open_shared(hC, "LC5");
    REQUIRE(AdsAppendRecord(hT) == 0);   // auto-locks the new record
    CHECK(num_locks(hT) >= 1u);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsUnlockTable(hT) == 0);
    CHECK(num_locks(hT) == 0u);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull local: lock, unlock, relock leaves no residue") {
    auto dir = fs::temp_directory_path() / "oads_lf_relock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    auto hC = connect_local(dir);
    stage_table(dir, "LC6", 5, hC);
    auto hT = open_shared(hC, "LC6");
    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsLockRecord(hT, 2) == 0);
        REQUIRE(AdsUnlockRecord(hT, 2) == 0);
        CHECK(num_locks(hT) == 0u);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// REMOTE
// ===========================================================================
TEST_CASE("lockfull remote: lock current / unlock current over the wire") {
    auto dir = fs::temp_directory_path() / "oads_lf_remote_cur";
    std::error_code ec;
    fs::remove_all(dir, ec);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hSetup = connect_local(dir);
    stage_table(dir, "RC1", 5, hSetup);
    REQUIRE(AdsDisconnect(hSetup) == 0);
    ADSHANDLE hC = connect_remote(dir, srv.port());
    auto hT = open_shared(hC, "RC1");
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsLockRecord(hT, 0) == 0);
    CHECK(num_locks(hT) == 1u);
    REQUIRE(AdsUnlockRecord(hT, 0) == 0);
    CHECK(num_locks(hT) == 0u);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull remote: dbUnlock (UnlockTable) releases the record lock") {
    auto dir = fs::temp_directory_path() / "oads_lf_remote_dbunlock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hSetup = connect_local(dir);
    stage_table(dir, "RC2", 5, hSetup);
    REQUIRE(AdsDisconnect(hSetup) == 0);
    ADSHANDLE hC = connect_remote(dir, srv.port());
    auto hT = open_shared(hC, "RC2");
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsLockRecord(hT, 0) == 0);      // current record, as dbRLock()
    CHECK(num_locks(hT) == 1u);
    REQUIRE(AdsUnlockTable(hT) == 0);        // dbUnlock()
    CHECK(num_locks(hT) == 0u);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull remote: contention blocks then clears after unlock-all") {
    auto dir = fs::temp_directory_path() / "oads_lf_remote_contend";
    std::error_code ec;
    fs::remove_all(dir, ec);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hSetup = connect_local(dir);
    stage_table(dir, "RC3", 5, hSetup);
    REQUIRE(AdsDisconnect(hSetup) == 0);

    ADSHANDLE hA = connect_remote(dir, srv.port());
    ADSHANDLE hB = connect_remote(dir, srv.port());
    auto hTA = open_shared(hA, "RC3");
    auto hTB = open_shared(hB, "RC3");

    REQUIRE(AdsLockRecord(hTA, 2) == 0);     // A holds record 2
    // B's attempt must fail (contention) — it burns its retry budget.
    CHECK(AdsLockRecord(hTB, 2) != 0);
    // A releases everything (dbUnlock semantics).
    REQUIRE(AdsUnlockTable(hTA) == 0);
    // B can now lock.
    REQUIRE(AdsLockRecord(hTB, 2) == 0);
    CHECK(num_locks(hTB) == 1u);
    REQUIRE(AdsUnlockTable(hTB) == 0);

    REQUIRE(AdsCloseTable(hTA) == 0);
    REQUIRE(AdsCloseTable(hTB) == 0);
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("lockfull remote: threads on a shared connection don't leak locks") {
    // The field scenario: N threads, ONE shared connection, each with its
    // own remote table over the same file, cycling lock/write/unlock.
    // With the SAP unlock-all semantic nothing leaks; every cycle wins.
    // Realistic pacing (a ms between cycles) — a tight spin would starve
    // losers past the ACE retry budget on scheduling alone, which is not
    // what this test is about.
    auto dir = fs::temp_directory_path() / "oads_lf_remote_threads";
    std::error_code ec;
    fs::remove_all(dir, ec);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hSetup = connect_local(dir);
    stage_table(dir, "RC4", 5, hSetup);
    REQUIRE(AdsDisconnect(hSetup) == 0);
    ADSHANDLE hC = connect_remote(dir, srv.port());

    constexpr int kThreads = 4;
    constexpr int kCycles = 8;
    std::atomic<int> failures{0};
    std::atomic<int> opened{0};
    // Open all four remote tables BEFORE any lock cycle: a thread whose
    // first lock collides while another thread's OpenTable occupies the
    // session would burn its ACE retry budget during the open storm —
    // that's scheduling, not a leak. The barrier removes it from the
    // lock window.
    std::vector<ADSHANDLE> handles(kThreads, 0);
    {
        std::vector<std::thread> openers;
        for (int t = 0; t < kThreads; ++t) {
            openers.emplace_back([&, t]() {
                UNSIGNED8 leaf[] = "RC4";
                if (AdsOpenTable(hC, leaf, leaf, ADS_CDX, 0, 0, 0, 0,
                                 &handles[t]) == 0)
                    ++opened;
            });
        }
        for (auto& th : openers) th.join();
    }
    REQUIRE(opened.load() == kThreads);

    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        pool.emplace_back([&, t]() {
            ADSHANDLE hT = handles[t];
            for (int c = 0; c < kCycles; ++c) {
                const UNSIGNED32 rec = 1 + (c % 5);
                UNSIGNED32 rc = AdsLockRecord(hT, rec);
                if (rc != 0) { fprintf(stderr, "[lockfull] lock rc=%u\n", rc); ++failures; break; }
                UNSIGNED8 f[] = "VAL";
                rc = AdsSetLong(hT, f, c);
                if (rc != 0) { fprintf(stderr, "[lockfull] setlong rc=%u\n", rc); ++failures; break; }
                rc = AdsWriteRecord(hT);
                if (rc != 0) { fprintf(stderr, "[lockfull] writerecord rc=%u\n", rc); ++failures; break; }
                rc = AdsUnlockTable(hT);
                if (rc != 0) { fprintf(stderr, "[lockfull] unlocktable rc=%u\n", rc); ++failures; break; }  // dbUnlock()
                UNSIGNED16 nl = 0xFFFF;
                if (AdsGetNumLocks(hT, &nl) != 0 || nl != 0) { fprintf(stderr, "[lockfull] residue nl=%u\n", nl); ++failures; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }
    for (auto& th : pool) th.join();
    for (ADSHANDLE hT : handles) AdsCloseTable(hT);
    CHECK(failures.load() == 0);
    REQUIRE(AdsDisconnect(hC) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}
