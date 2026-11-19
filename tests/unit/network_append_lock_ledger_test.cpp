// network_append_lock_ledger_test.cpp -- the reporting user finding 2a (2026-10-25):
// EXCLUSIVE or FLOCKed appends over REMOTE stopped at 4,096 records with
// "Session record-lock limit" [32/7079]. Two stacked causes:
//   - Session::AppendBlank registered every automatic append lock in
//     explicit_record_locks_ unconditionally (hardening), and Harbour
//     rddads never sends an unlock in exclusive/FLock modes (adsRawLock
//     early-returns), so the ledger only grew.
//   - Table::append_blank took the auto-lock in every mode except
//     exclusive ADT, filling the engine's own 4096 recno registry the
//     same way.
// SAP ADS takes no record locks on an exclusive or file-locked table
// (Harbour dbfRawLock: REC_LOCK is a no-op success there), and the 5035
// write guard already bypasses both modes. The fix skips the append
// auto-lock (and its ledger registration) under whole-file cover and
// mirrors a virtual FLock onto the parallel ABI twin so its write guard
// still passes. Shared mode must keep the auto-lock.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::AE_SUCCESS;

namespace {

constexpr int kBulk = 4200;  // past the 4096 ledger/registry walls

fs::path ledger_tmp_dir() {
    return fs::temp_directory_path() / "openads_append_lock_ledger";
}

// Recreates <tmp>/ap.dbf with 3 rows, NAME C(8), plus ap.cdx tag BYNAME.
void seed_ledger_fixture(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    const auto d = dir.string();
    std::memcpy(srv, d.c_str(), d.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    UNSIGNED8 tname[] = "ap.dbf";
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
    ADSHANDLE hIdx = 0;
    UNSIGNED8 bag[] = "ap.cdx";
    UNSIGNED8 tag[] = "BYNAME";
    UNSIGNED8 exp[] = "NAME";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

ADSHANDLE connect_remote(const UNSIGNED8* srvbuf) {
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(const_cast<UNSIGNED8*>(srvbuf), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) == AE_SUCCESS);
    return h;
}

ADSHANDLE open_remote(ADSHANDLE hConn, const char* name, UNSIGNED16 mode) {
    ADSHANDLE h = 0;
    REQUIRE(AdsOpenTable(hConn,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                         nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, mode, &h)
            == AE_SUCCESS);
    return h;
}

void append_write(ADSHANDLE h, int i) {
    char v[9];
    std::snprintf(v, sizeof(v), "N%07d", i);
    UNSIGNED8 fld[] = "NAME";
    REQUIRE(AdsAppendRecord(h) == AE_SUCCESS);
    REQUIRE(AdsSetString(h, fld, reinterpret_cast<UNSIGNED8*>(v), 8)
            == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(h) == AE_SUCCESS);
}

} // namespace

TEST_CASE("2a exclusive remote append passes the 4096 wall") {
    auto dir = ledger_tmp_dir();
    seed_ledger_fixture(dir);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);

    ADSHANDLE hC = connect_remote(sb);
    ADSHANDLE hT = open_remote(hC, "ap.dbf", ADS_EXCLUSIVE);
    for (int i = 0; i < kBulk; ++i) append_write(hT, i);
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hT, 0, &n) == AE_SUCCESS);
    REQUIRE(n == static_cast<UNSIGNED32>(kBulk + 3));
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
}

TEST_CASE("2a flocked remote append+write passes 4096, no zombie locks") {
    auto dir = ledger_tmp_dir();
    seed_ledger_fixture(dir);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);

    ADSHANDLE hC = connect_remote(sb);
    ADSHANDLE hT = open_remote(hC, "ap.dbf", ADS_SHARED);
    // Force the parallel ABI twin before the FLock (LockTable mirror path).
    ADSHANDLE hIdx = 0;
    UNSIGNED16 nIdx = 0;
    UNSIGNED8 bag[] = "ap.cdx";
    REQUIRE(AdsOpenIndex(hT, bag, &hIdx, &nIdx) == AE_SUCCESS);
    REQUIRE(AdsLockTable(hT) == AE_SUCCESS);
    // Writes to the freshly appended rows exercise the twin's 5035 guard
    // under the mirrored virtual FLock.
    for (int i = 0; i < kBulk; ++i) append_write(hT, i);
    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);

    // No zombie record locks may survive the FLock release: a peer session
    // must lock an appended record at once.
    ADSHANDLE hC2 = connect_remote(sb);
    ADSHANDLE hT2 = open_remote(hC2, "ap.dbf", ADS_SHARED);
    REQUIRE(AdsLockRecord(hT2, 10) == AE_SUCCESS);
    REQUIRE(AdsUnlockRecord(hT2, 10) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT2) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC2) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
}

TEST_CASE("2a twin born under an existing flock is covered too") {
    auto dir = ledger_tmp_dir();
    seed_ledger_fixture(dir);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);

    ADSHANDLE hC = connect_remote(sb);
    ADSHANDLE hT = open_remote(hC, "ap.dbf", ADS_SHARED);
    REQUIRE(AdsLockTable(hT) == AE_SUCCESS);
    // Twin created only now (ensure_abi_handle grant path).
    ADSHANDLE hIdx = 0;
    UNSIGNED16 nIdx = 0;
    UNSIGNED8 bag[] = "ap.cdx";
    REQUIRE(AdsOpenIndex(hT, bag, &hIdx, &nIdx) == AE_SUCCESS);
    for (int i = 0; i < 200; ++i) append_write(hT, i);
    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
}

TEST_CASE("2a shared mode keeps the append auto-lock") {
    auto dir = ledger_tmp_dir();
    seed_ledger_fixture(dir);
    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);

    ADSHANDLE hC = connect_remote(sb);
    ADSHANDLE hT = open_remote(hC, "ap.dbf", ADS_SHARED);
    // rddads shared-mode cadence: append releases the previous lock.
    for (int i = 0; i < 50; ++i) {
        append_write(hT, i);
        REQUIRE(AdsUnlockRecord(hT, 0) == AE_SUCCESS);
    }
    // An append with no unlock must still hold the auto-lock.
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    UNSIGNED16 locked = 0;
    REQUIRE(AdsIsRecordLocked(hT, 0, &locked) == AE_SUCCESS);
    REQUIRE(locked == 1);
    REQUIRE(AdsUnlockRecord(hT, 0) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
}
