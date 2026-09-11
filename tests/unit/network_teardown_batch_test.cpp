// tests/unit/network_teardown_batch_test.cpp
// USE-teardown batching (Vouch startup: FlushFileBuffers +
// CloseAllIndexes before every CloseTable ≈ 2 wasted frames per USE,
// CheckExistence per USE, repeated SetOrder, uncached order keycounts).
//
//  - Flush/CloseAll defer to the close that absorbs them (server close
//    flushes via its shadow handle and purges index bindings); any
//    other intervening wire op flushes them first, in order.
//  - CheckExistence serves positive answers locally until a
//    file-mutating op clears the cache (negatives always go out).
//  - SetOrder to the ack-confirmed binding skips its frame (SetOrder
//    never moves the cursor).
//  - Order-handle key counts ride the parent's order cache.

#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uint8_t kOpCloseTable   = 0x22;
constexpr std::uint8_t kOpFlushFile    = 0x7E;
constexpr std::uint8_t kOpCloseAllIdx  = 0x80;
constexpr std::uint8_t kOpSetOrder     = 0x8C;
constexpr std::uint8_t kOpKeyCount     = 0xB0;
constexpr std::uint8_t kOpFileExists   = 0xE0;

fs::path tb_tmp_dir() {
    return fs::temp_directory_path() / "openads_teardown_test";
}

void tb_wipe() {
    std::error_code ec;
    fs::remove_all(tb_tmp_dir(), ec);
    fs::create_directories(tb_tmp_dir(), ec);
}

void tb_seed(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    UNSIGNED8 def[]   = "ID,N,8,0";
    UNSIGNED8 tname[] = "TB.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, fld, i * 10) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "TB.CDX";
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::uint64_t tb_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op].count.load(std::memory_order_relaxed);
}

ADSHANDLE tb_connect_remote(const fs::path& dir, std::uint16_t port) {
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

ADSHANDLE tb_open(ADSHANDLE hConn) {
    UNSIGNED8 tname[] = "TB.DBF";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    return hTable;
}

} // namespace

TEST_CASE("Teardown batching: flush+closeall absorb into close") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    const std::uint64_t fl0 = tb_op(kOpFlushFile);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t cl0 = tb_op(kOpCloseTable);

    // rddads' per-USE teardown triple: neither teardown frame goes out;
    // the close absorbs both.
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpFlushFile) == fl0);
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpCloseTable) == cl0 + 1);

    // Absorbed close still fully closed: reopen navigates the order.
    ADSHANDLE hT2 = tb_open(hConn);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hT2, want, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hT2, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 1u);
    REQUIRE(AdsCloseTable(hT2) == AE_SUCCESS);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: intervening op flushes teardown first") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    const std::uint64_t fl0 = tb_op(kOpFlushFile);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);

    // CloseAll, then a real nav: the deferred teardown goes out ahead
    // of it — merged into a single CloseAllIndexes frame (the server
    // flushes inside that handler).
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpFlushFile) == fl0);
    CHECK(tb_op(kOpCloseAllIdx) == ca0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: existence positives cache until mutation") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv.set_enable_file_func(true);
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());

    const std::uint64_t fe0 = tb_op(kOpFileExists);
    UNSIGNED8 fn[] = "TB.DBF";
    UNSIGNED16 ex = 0;
    REQUIRE(AdsCheckExistence(hConn, fn, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    REQUIRE(AdsCheckExistence(hConn, fn, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    REQUIRE(AdsCheckExistence(hConn, fn, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(tb_op(kOpFileExists) == fe0 + 1);

    // A file-mutating op clears the cache: the next check goes out and
    // reports the drop.
    REQUIRE(AdsDropTable(hConn, fn, 1) == AE_SUCCESS);
    REQUIRE(AdsCheckExistence(hConn, fn, &ex) == AE_SUCCESS);
    CHECK(ex == 0u);
    CHECK(tb_op(kOpFileExists) == fe0 + 2);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: SetOrder defers until use") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    // SetOrder alone sends nothing (deferred); repeats overwrite the
    // pending switch, by name or by handle alike.
    const std::uint64_t so0 = tb_op(kOpSetOrder);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrder(hTable, want) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);

    // The following nav absorbs it (fused frame — see nav_batch);
    // order lands correctly.
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 1u);

    // Now the server binding matches: a repeat skips outright.
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: order key counts ride the parent cache") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);

    const std::uint64_t kc0 = tb_op(kOpKeyCount);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    CHECK(tb_op(kOpKeyCount) == kc0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
