// tests/unit/network_commit_slimming_test.cpp
// Commit-path slimming for the Vouch WAN voucher save (mtfix8):
//   a) AdsUnlockTable with an empty client lock ledger sends no frame
//      (rddads dbUnlock() calls it blindly before every lock/append);
//   b) AdsGetAllLocks answers from the ledger while it is provably
//      complete (no append auto-lock outstanding, no table lock);
//   c) AdsWriteRecord on a clean table sends no FlushTable frame
//      (DBCOMMITALL touches every workarea), and on a
//      kCapFlushTableDurable server the trailing AdsFlushFileBuffers
//      owes nothing either -- one frame per commit;
//   d) a table whose locks were all released parks at close instead
//      of real-closing (the blunt ever_locked flag used to force a
//      7-frame reopen per save);
//   e) File and directory existence always re-probe the server; external
//      sessions and host operations may change them.
// mtfix10:
//   f) AdsGetNumLocks shares the GetAllLocks ledger fast path (rddads
//      polls it after every commit/unlock -- it used to ride the wire
//      op even when the ledger was provably complete);
//   g) FileExists no longer uses an open-table shortcut: it checks the
//      exact path even when a table or index with that stem is open.
#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uint8_t kOpOpenTable   = 0x20;
constexpr std::uint8_t kOpCloseTable  = 0x22;
constexpr std::uint8_t kOpGetAllLocks = 0x15;
constexpr std::uint8_t kOpFlushTable  = 0x5A;
constexpr std::uint8_t kOpUnlockTable = 0x78;
constexpr std::uint8_t kOpFlushFile   = 0x7E;
constexpr std::uint8_t kOpFileExists  = 0xE0;
constexpr std::uint8_t kOpDirExist    = 0xEC;
constexpr std::uint8_t kOpDirMake     = 0xEE;

fs::path cs_tmp_dir() {
    return fs::temp_directory_path() / "openads_commit_slim_test";
}

void cs_wipe() {
    std::error_code ec;
    fs::remove_all(cs_tmp_dir(), ec);
    fs::create_directories(cs_tmp_dir(), ec);
}

void cs_seed(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    UNSIGNED8 def[]   = "ID,N,8,0";
    UNSIGNED8 tname[] = "CS.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, fld, i * 10) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::uint64_t cs_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op].count.load(std::memory_order_relaxed);
}

ADSHANDLE cs_connect_remote(const fs::path& dir, std::uint16_t port) {
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(port), dir.string().c_str());
    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &hConn) == AE_SUCCESS);
    return hConn;
}

ADSHANDLE cs_open(ADSHANDLE hConn) {
    UNSIGNED8 tname[] = "CS.DBF";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    return hTable;
}

} // namespace

TEST_CASE("Commit slimming: blind UnlockTable and GetAllLocks stay local") {
    cs_wipe();
    auto dir = cs_tmp_dir();
    cs_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = cs_connect_remote(dir, srv.port());
    ADSHANDLE hTable = cs_open(hConn);

    // Fresh table, nothing held: dbUnlock() costs nothing.
    std::uint64_t un0 = cs_op(kOpUnlockTable);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpUnlockTable) == un0);

    // A held lock forces the real frame; then the ledger is clear and
    // the next blind unlock is free again.
    REQUIRE(AdsLockRecord(hTable, 1) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpUnlockTable) == un0 + 1);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpUnlockTable) == un0 + 1);

    // GetAllLocks answers from the ledger with the right recnos.
    REQUIRE(AdsLockRecord(hTable, 1) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hTable, 3) == AE_SUCCESS);
    const std::uint64_t gal0 = cs_op(kOpGetAllLocks);
    UNSIGNED32 recs[8]{};
    UNSIGNED16 n = 8;
    REQUIRE(AdsGetAllLocks(hTable, recs, &n) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0);
    CHECK(n == 2u);

    // GetNumLocks shares the ledger fast path (mtfix10).
    UNSIGNED16 nl = 0;
    REQUIRE(AdsGetNumLocks(hTable, &nl) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0);
    CHECK(nl == 2u);
    bool saw1 = false, saw3 = false;
    for (UNSIGNED16 i = 0; i < n && i < 8; ++i) {
        saw1 = saw1 || recs[i] == 1u;
        saw3 = saw3 || recs[i] == 3u;
    }
    CHECK(saw1);
    CHECK(saw3);

    // An append auto-locks server-side with no recno in the ack: the
    // ledger goes uncertain, GetAllLocks returns to the wire, and the
    // following UnlockTable really goes out (and releases it).
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    n = 8;
    REQUIRE(AdsGetAllLocks(hTable, recs, &n) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0 + 1);
    CHECK(n >= 3u);   // 1, 3 and the auto-locked blank
    nl = 0;
    REQUIRE(AdsGetNumLocks(hTable, &nl) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0 + 2);   // uncertain: back to wire
    CHECK(nl >= 3u);
    un0 = cs_op(kOpUnlockTable);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpUnlockTable) == un0 + 1);

    // Ledger provably empty again: local answer, zero locks.
    n = 8;
    REQUIRE(AdsGetAllLocks(hTable, recs, &n) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0 + 2);
    CHECK(n == 0u);
    nl = 99;
    REQUIRE(AdsGetNumLocks(hTable, &nl) == AE_SUCCESS);
    CHECK(cs_op(kOpGetAllLocks) == gal0 + 2);
    CHECK(nl == 0u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Commit slimming: clean commit and durable FlushTable") {
    cs_wipe();
    auto dir = cs_tmp_dir();
    cs_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = cs_connect_remote(dir, srv.port());
    ADSHANDLE hTable = cs_open(hConn);

    // Nothing written since open: COMMIT sends no FlushTable.
    const std::uint64_t ft0 = cs_op(kOpFlushTable);
    const std::uint64_t ff0 = cs_op(kOpFlushFile);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpFlushTable) == ft0);

    // A real write: FlushTable goes out once, and the durable server
    // makes the trailing FlushFileBuffers a no-op.
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsLockRecord(hTable, 2) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hTable, fld, 99.0) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpFlushTable) == ft0 + 1);
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpFlushFile) == ff0);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);

    // The write really landed: a fresh open reads 99 back.
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    ADSHANDLE hT2 = cs_open(hConn);
    REQUIRE(AdsGotoRecord(hT2, 2) == AE_SUCCESS);
    double val = 0.0;
    REQUIRE(AdsGetDouble(hT2, fld, &val) == AE_SUCCESS);
    CHECK(val == 99.0);
    REQUIRE(AdsCloseTable(hT2) == AE_SUCCESS);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Commit slimming: released-lock tables park at close") {
    cs_wipe();
    auto dir = cs_tmp_dir();
    cs_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = cs_connect_remote(dir, srv.port());
    ADSHANDLE hTable = cs_open(hConn);

    // Lock, unlock, close: nothing held at close, so the table parks
    // (no CloseTable frame) and the reopen is a park hit.
    REQUIRE(AdsLockRecord(hTable, 4) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hTable) == AE_SUCCESS);
    const std::uint64_t cl0 = cs_op(kOpCloseTable);
    const std::uint64_t op0 = cs_op(kOpOpenTable);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    CHECK(cs_op(kOpCloseTable) == cl0);

    ADSHANDLE hT2 = cs_open(hConn);
    CHECK(cs_op(kOpOpenTable) == op0);
    REQUIRE(AdsGotoRecord(hT2, 4) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    double val = 0.0;
    REQUIRE(AdsGetDouble(hT2, fld, &val) == AE_SUCCESS);
    CHECK(val == 40.0);
    REQUIRE(AdsCloseTable(hT2) == AE_SUCCESS);

    // Append + write + unlock + close: the auto-lock is released, so
    // this parks too -- the old ever_locked rule real-closed here.
    ADSHANDLE hT3 = cs_open(hConn);
    REQUIRE(AdsAppendRecord(hT3) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT3, fld, 60.0) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT3) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hT3) == AE_SUCCESS);
    const std::uint64_t cl1 = cs_op(kOpCloseTable);
    REQUIRE(AdsCloseTable(hT3) == AE_SUCCESS);
    CHECK(cs_op(kOpCloseTable) == cl1);

    // A lock STILL held at close keeps the real close (no park leak).
    ADSHANDLE hT4 = cs_open(hConn);
    REQUIRE(AdsLockRecord(hT4, 1) == AE_SUCCESS);
    const std::uint64_t cl2 = cs_op(kOpCloseTable);
    REQUIRE(AdsCloseTable(hT4) == AE_SUCCESS);
    CHECK(cs_op(kOpCloseTable) == cl2 + 1);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Commit slimming: directory state always reflects server changes") {
    cs_wipe();
    auto dir = cs_tmp_dir();
    cs_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv.set_enable_file_func(true);
    ADSHANDLE hConn = cs_connect_remote(dir, srv.port());

    const std::uint64_t de0 = cs_op(kOpDirExist);
    const std::uint64_t dm0 = cs_op(kOpDirMake);
    UNSIGNED16 ex = 0;
    REQUIRE(AdsDirExist(hConn, (UNSIGNED8*)".", &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    REQUIRE(AdsDirExist(hConn, (UNSIGNED8*)".", &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(cs_op(kOpDirExist) == de0 + 2);
    REQUIRE(AdsDirMake(hConn, (UNSIGNED8*)".") == AE_SUCCESS);
    CHECK(cs_op(kOpDirMake) == dm0 + 1);

    REQUIRE(AdsDirMake(hConn, (UNSIGNED8*)"cs_sub") == AE_SUCCESS);
    CHECK(cs_op(kOpDirMake) == dm0 + 2);
    REQUIRE(fs::is_directory(dir / "cs_sub"));
    REQUIRE(AdsDirExist(hConn, (UNSIGNED8*)"cs_sub", &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(cs_op(kOpDirExist) == de0 + 3);

    // Simulate host maintenance or a second client: deletion bypasses this
    // connection's DirRemove, so its next probe must reach the server.
    std::error_code ec;
    REQUIRE(fs::remove(dir / "cs_sub", ec));
    REQUIRE_FALSE(ec);
    REQUIRE(AdsDirExist(hConn, (UNSIGNED8*)"cs_sub", &ex) == AE_SUCCESS);
    CHECK(ex == 0u);
    CHECK(cs_op(kOpDirExist) == de0 + 4);
    REQUIRE(AdsDirMake(hConn, (UNSIGNED8*)"cs_sub") == AE_SUCCESS);
    CHECK(cs_op(kOpDirMake) == dm0 + 3);
    CHECK(fs::is_directory(dir / "cs_sub"));
    REQUIRE(AdsDirExist(hConn, (UNSIGNED8*)"cs_sub", &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(cs_op(kOpDirExist) == de0 + 5);

    // A repeat mkdir must still reach the server (and remain idempotent),
    // not claim success from an obsolete positive answer.
    REQUIRE(fs::remove(dir / "cs_sub", ec));
    REQUIRE_FALSE(ec);
    REQUIRE(AdsDirMake(hConn, (UNSIGNED8*)"cs_sub") == AE_SUCCESS);
    CHECK(cs_op(kOpDirMake) == dm0 + 4);
    CHECK(fs::is_directory(dir / "cs_sub"));
    REQUIRE(AdsDirRemove(hConn, (UNSIGNED8*)"cs_sub") == AE_SUCCESS);

    // FileExists is also a live probe. A positive answer for an open table
    // must not bypass the server, nor may a missing bag be cached.
    ADSHANDLE hTable = cs_open(hConn);
    const std::uint64_t fe0 = cs_op(kOpFileExists);
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"CS.DBF", &ex)
            == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(cs_op(kOpFileExists) == fe0 + 1);
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"CS.Z01", &ex)
            == AE_SUCCESS);
    CHECK(ex == 0u);
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"CS.Z01", &ex)
            == AE_SUCCESS);
    CHECK(ex == 0u);
    CHECK(cs_op(kOpFileExists) == fe0 + 3);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Positive-to-missing and missing-to-positive after external changes
    // on the server, without a mutation through this connection.
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"cs_sub", &ex)
            == AE_SUCCESS);
    CHECK(ex == 0u);
    REQUIRE(fs::create_directory(dir / "cs_sub", ec));
    REQUIRE_FALSE(ec);
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"cs_sub", &ex)
            == AE_SUCCESS);
    CHECK(ex == 1u);
    REQUIRE(fs::remove(dir / "cs_sub", ec));
    REQUIRE_FALSE(ec);
    REQUIRE(AdsCheckExistence(hConn, (UNSIGNED8*)"cs_sub", &ex)
            == AE_SUCCESS);
    CHECK(ex == 0u);
    CHECK(cs_op(kOpFileExists) == fe0 + 6);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
