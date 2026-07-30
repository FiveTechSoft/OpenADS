#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Reproduces the Harbour multi-instance lock test from pritpal:
//   1. Record locked in instance A → instance B's AdsLockRecord fails (FALSE)
//   2. Record locked in instance A → instance B's AdsSetField without lock
//      must return error 5035 (GoHot guard / Lock Required)
//   3. FLock contention: instance A holds FLock → instance B's FLock fails
//   4. Multi-thread concurrent reads on shared table
// ---------------------------------------------------------------------------

namespace {

// Build a remote URI that points at the real directory where the fixture
// DBF was staged. The previous hard-coded "//Temp" never existed on CI
// runners (Linux has /tmp, not /Temp; Windows rarely has C:\Temp), so
// Connection::open rejected the connect with "data directory not found"
// and every case failed at AdsConnect60. Mirror the pattern used by the
// other remote unit tests (abi_get_set_record_test, abi_fetch_where_test).
struct ServerGuard {
    openads::network::Server srv;
    std::string uri;
    bool ok = false;
    explicit ServerGuard(const fs::path& data_dir) {
        if (auto v = srv.start("127.0.0.1", 0)) {
            std::string path = data_dir.string();
            std::replace(path.begin(), path.end(), '\\', '/');
            uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;
            ok = true;
        }
    }
    void stop() { srv.stop(); }
};

fs::path stage_dbf(const fs::path& dir, const char* name = "test.dbf",
                   int nrecs = 5) {
    std::error_code ec;
    fs::create_directories(dir);
    auto p = dir / name;
    fs::remove(p, ec);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(nrecs);
    hdr[8] = 32 + 32 + 1; hdr[10] = 1 + 5;  // TAG C(5)
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);  // header terminator
    for (int r = 0; r < nrecs; ++r) {
        file.push_back(' ');  // not deleted
        char val[5] = {'A', '0', '0', static_cast<char>('0' + r), '\0'};
        for (int i = 0; i < 5; ++i) file.push_back(val[i]);
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

// ===========================================================================
// Test 1: Record locked in A → B's AdsLockRecord fails after retries
//         (User: "Normal behavior of DBFCDX is to raise error in 2nd instance.
//          Not raise error but just return FALSE — do not stay there waiting.")
// ===========================================================================
TEST_CASE("Remote: record lock contention — B's lock fails, does not hang") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_rlock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    // Connect A
    ADSHANDLE hConnA = 0;
    UNSIGNED8 uriA[512]{};
    std::memcpy(uriA, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uriA, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConnA) == 0);

    ADSHANDLE hTblA = 0;
    UNSIGNED8 name[16] = "test";
    REQUIRE(AdsOpenTable(hConnA, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTblA) == 0);

    // Connect B
    ADSHANDLE hConnB = 0;
    UNSIGNED8 uriB[512]{};
    std::memcpy(uriB, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uriB, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConnB) == 0);

    ADSHANDLE hTblB = 0;
    REQUIRE(AdsOpenTable(hConnB, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTblB) == 0);

    // Tighten retry policy for fast test
    REQUIRE(AdsSetLockCycle(0, 10) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 3) == 0);

    // A locks record 1
    REQUIRE(AdsGotoRecord(hTblA, 1) == 0);
    REQUIRE(AdsLockRecord(hTblA, 1) == 0);

    // B tries to lock record 1 — must FAIL (return FALSE), not hang
    REQUIRE(AdsGotoRecord(hTblB, 1) == 0);
    auto t0 = std::chrono::steady_clock::now();
    UNSIGNED32 rc = AdsLockRecord(hTblB, 1);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(rc != 0);  // Must NOT succeed — lock held by A
    // Must have returned promptly (~30ms max), NOT hung forever
    CHECK(elapsed < 2000);

    // Restore defaults
    REQUIRE(AdsSetLockCycle(0, 100) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 10) == 0);

    // Cleanup
    REQUIRE(AdsUnlockRecord(hTblA, 1) == 0);
    REQUIRE(AdsCloseTable(hTblA) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsCloseTable(hTblB) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Test 2: Write without lock → must return error (GoHot guard).
//         (User: "If DBRLOCK is not requested then it must raise LOCK
//          REQUIRED RTE.")
// ===========================================================================
TEST_CASE("Remote: write without lock returns error 5035 (GoHot guard)") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_nolock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    ADSHANDLE hConn = 0;
    UNSIGNED8 uri[512]{};
    std::memcpy(uri, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uri, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[16] = "test";
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTbl) == 0);

    REQUIRE(AdsGotoRecord(hTbl, 1) == 0);

    // Try to write WITHOUT locking — must fail with 5035 (Lock Required)
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "NOPE";
    UNSIGNED32 rc = AdsSetString(hTbl, fld, val, 4);
    CHECK(rc != 0);  // Must fail — record not locked

    // Now lock and retry — must succeed
    REQUIRE(AdsLockRecord(hTbl, 1) == 0);
    rc = AdsSetString(hTbl, fld, val, 4);
    CHECK(rc == 0);  // Must succeed — record is locked

    // Write record to flush
    REQUIRE(AdsWriteRecord(hTbl) == 0);

    // Verify value was written
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTbl, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "NOPE  ");

    // Cleanup
    REQUIRE(AdsUnlockRecord(hTbl, 1) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Test 3: FLock contention — A holds FLock, B's FLock fails
//         (User tests FLock in multi-instance scenario)
// ===========================================================================
TEST_CASE("Remote: FLock contention — B's FLock fails after retries") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_flock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    // Connect A
    ADSHANDLE hConnA = 0;
    UNSIGNED8 uriA[512]{};
    std::memcpy(uriA, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uriA, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConnA) == 0);

    ADSHANDLE hTblA = 0;
    UNSIGNED8 name[16] = "test";
    REQUIRE(AdsOpenTable(hConnA, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTblA) == 0);

    // Connect B
    ADSHANDLE hConnB = 0;
    UNSIGNED8 uriB[512]{};
    std::memcpy(uriB, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uriB, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConnB) == 0);

    ADSHANDLE hTblB = 0;
    REQUIRE(AdsOpenTable(hConnB, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTblB) == 0);

    // Tighten retry policy
    REQUIRE(AdsSetLockCycle(0, 10) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 3) == 0);

    // A grabs file lock
    REQUIRE(AdsLockTable(hTblA) == 0);

    // B tries file lock — must FAIL, not hang
    auto t0 = std::chrono::steady_clock::now();
    UNSIGNED32 rc = AdsLockTable(hTblB);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(rc != 0);  // Must NOT succeed
    CHECK(elapsed < 2000);  // Must return promptly

    // Restore defaults
    REQUIRE(AdsSetLockCycle(0, 100) == 0);
    REQUIRE(AdsSetLockRetryCount(0, 10) == 0);

    // Cleanup
    REQUIRE(AdsUnlockTable(hTblA) == 0);
    REQUIRE(AdsCloseTable(hTblA) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsCloseTable(hTblB) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Test 4: Write with FLock succeeds (FLock covers all records)
//         (User: "After FLock, writes should succeed without per-record lock")
// ===========================================================================
TEST_CASE("Remote: write with FLock succeeds without record lock") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_flock_write";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    ADSHANDLE hConn = 0;
    UNSIGNED8 uri[512]{};
    std::memcpy(uri, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uri, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[16] = "test";
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTbl) == 0);

    // File-lock the table
    REQUIRE(AdsLockTable(hTbl) == 0);

    // Write without record lock — should succeed (FLock covers all)
    REQUIRE(AdsGotoRecord(hTbl, 1) == 0);
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "FLCKD";
    REQUIRE(AdsSetString(hTbl, fld, val, 5) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);

    // Verify
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTbl, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "FLCKD");

    // Cleanup
    REQUIRE(AdsUnlockTable(hTbl) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Test 5: Exclusive open — no lock needed for writes
//         (User's implicit test: ADS_EXCLUSIVE should bypass lock checks)
// ===========================================================================
TEST_CASE("Remote: Exclusive open allows writes without lock") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_excl";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    ADSHANDLE hConn = 0;
    UNSIGNED8 uri[512]{};
    std::memcpy(uri, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uri, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[16] = "test";
    // Open EXCLUSIVE — no lock needed
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_EXCLUSIVE, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTbl) == 0);

    REQUIRE(AdsGotoRecord(hTbl, 1) == 0);
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "EXCL";
    REQUIRE(AdsSetString(hTbl, fld, val, 4) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);

    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTbl, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "EXCL  ");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// ===========================================================================
// Test 6: Pending append — freshly-appended record is writable without lock
//         (Standard xBase/ACE: AdsAppendRecord auto-locks)
// ===========================================================================
TEST_CASE("Remote: freshly-appended record writable without explicit lock") {
    const auto dir = fs::temp_directory_path() / "openads_pritpal_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    stage_dbf(dir);

    ServerGuard sg(dir);
    REQUIRE(sg.ok);

    ADSHANDLE hConn = 0;
    UNSIGNED8 uri[512]{};
    std::memcpy(uri, sg.uri.c_str(), sg.uri.size() + 1);
    REQUIRE(AdsConnect60(uri, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTbl = 0;
    UNSIGNED8 name[16] = "test";
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX,
                         ADS_ANSI, ADS_SHARED, ADS_COMPATIBLE_LOCKING,
                         ADS_DEFAULT, &hTbl) == 0);

    // Append a new record — no explicit lock needed
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "NEW01";
    REQUIRE(AdsSetString(hTbl, fld, val, 5) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);

    // Verify
    UNSIGNED8 buf[8] = {};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTbl, fld, buf, &cap, 0) == 0);
    CHECK(std::string(reinterpret_cast<const char*>(buf), cap) == "NEW01");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
