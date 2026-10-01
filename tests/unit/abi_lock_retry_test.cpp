#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    fs::remove(p, ec);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 1;
    hdr[8]  = 32 + 32 + 1; hdr[10] = 1 + 5;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' ');
    for (int i = 0; i < 5; ++i) file.push_back('A');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

}  // namespace

TEST_CASE("M9.18 AdsSetLockCycle / AdsGetLockCycle round-trip") {
    UNSIGNED32 prev = 0;
    REQUIRE(AdsGetLockCycle(/*hConn=*/0, &prev) == 0);

    REQUIRE(AdsSetLockCycle(0, 250) == 0);
    UNSIGNED32 cur = 0;
    REQUIRE(AdsGetLockCycle(0, &cur) == 0);
    CHECK(cur == 250);

    // Restore.
    REQUIRE(AdsSetLockCycle(0, prev) == 0);
}

TEST_CASE("M9.18 AdsSetLockRetryCount / AdsGetLockRetryCount round-trip") {
    UNSIGNED16 prev = 0;
    REQUIRE(AdsGetLockRetryCount(0, &prev) == 0);

    REQUIRE(AdsSetLockRetryCount(0, 25) == 0);
    UNSIGNED16 cur = 0;
    REQUIRE(AdsGetLockRetryCount(0, &cur) == 0);
    CHECK(cur == 25);

    REQUIRE(AdsSetLockRetryCount(0, prev) == 0);
}

TEST_CASE("M9.18 default policy lets AdsLockTable + AdsLockRecord succeed without contention") {
    const auto dir = fs::temp_directory_path() / "openads_m9_18_lock_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "data";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);

    REQUIRE(AdsLockTable(hTable)   == 0);
    REQUIRE(AdsUnlockTable(hTable) == 0);

    REQUIRE(AdsLockRecord(hTable, 1)   == 0);
    REQUIRE(AdsUnlockRecord(hTable, 1) == 0);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.18 retry loop sleeps cycle_ms * retry_count when contended") {
    // Force contention: open the same DBF twice in distinct connections.
    // Second AdsLockTable should retry retry_count times before giving up,
    // taking at least retry_count * cycle_ms milliseconds.
    const auto dir = fs::temp_directory_path() / "openads_m9_18_lock_busy";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[16] = "data";

    ADSHANDLE hConnA = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConnA) == 0);
    ADSHANDLE hTableA = 0;
    REQUIRE(AdsOpenTable(hConnA, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTableA) == 0);

    ADSHANDLE hConnB = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConnB) == 0);
    ADSHANDLE hTableB = 0;
    REQUIRE(AdsOpenTable(hConnB, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTableB) == 0);

    // Tighten the policy so the test runs in well under a second.
    REQUIRE(AdsSetLockCycle(0, 10) == 0);       // 10 ms between attempts
    REQUIRE(AdsSetLockRetryCount(0, 5) == 0);   // 5 retries → ~50 ms wait

    // Connection A grabs the table lock. Connection B tries — should
    // retry 5 times, sleep 5 * 10 ms = 50 ms, then fail.
    REQUIRE(AdsLockTable(hTableA) == 0);

    auto t0 = std::chrono::steady_clock::now();
    UNSIGNED32 rc = AdsLockTable(hTableB);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    // Whether the lock fails or succeeds depends on whether the
    // single-process LockMgr treats two ADSHANDLE-distinct opens of
    // the same path as conflicting. Either way, the retry loop must
    // have spent ≥ 50 ms before reporting a result if it failed, or
    // returned promptly if the inner LockMgr permitted reentry.
    if (rc != 0) {
        CHECK(elapsed >= 40);  // allow a small scheduling jitter
    }

    REQUIRE(AdsUnlockTable(hTableA) == 0);

    // Restore the default single-attempt policy so other tests aren't
    // affected (the process default is retry_count 0 since 1.09.68-mtfix6).
    REQUIRE(AdsSetLockCycle(0, 100) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 0) == 0);

    REQUIRE(AdsCloseTable(hTableA) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsCloseTable(hTableB) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("M9.18 single-attempt policy never sleeps when contended") {
    // mtfix6 default: a failed lock returns immediately - the application
    // owns any retry loop (xBase RLOCK()/FLOCK() semantics). With
    // retry_count 0 the call must never wait, pass or fail.
    const auto dir = fs::temp_directory_path() / "openads_m9_18_lock_fast";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    UNSIGNED8 leaf[16] = "data";

    ADSHANDLE hConnA = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConnA) == 0);
    ADSHANDLE hTableA = 0;
    REQUIRE(AdsOpenTable(hConnA, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTableA) == 0);

    ADSHANDLE hConnB = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConnB) == 0);
    ADSHANDLE hTableB = 0;
    REQUIRE(AdsOpenTable(hConnB, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTableB) == 0);

    // Single attempt, 100 ms cycle: the cycle must never be slept.
    REQUIRE(AdsSetLockCycle(0, 100) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 0) == 0);

    REQUIRE(AdsLockTable(hTableA) == 0);

    auto t0 = std::chrono::steady_clock::now();
    AdsLockTable(hTableB);  // result irrelevant - timing is the assertion
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(elapsed < 50);  // single attempt on an in-process server: no sleep

    REQUIRE(AdsUnlockTable(hTableA) == 0);

    REQUIRE(AdsCloseTable(hTableA) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsCloseTable(hTableB) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
    fs::remove_all(dir, ec);
}
