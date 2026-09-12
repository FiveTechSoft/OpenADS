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
constexpr std::uint8_t kOpOpenIndex    = 0x88;
constexpr std::uint8_t kOpSetOrder     = 0x8C;
constexpr std::uint8_t kOpKeyCount     = 0xB0;
constexpr std::uint8_t kOpFileExists   = 0xE0;
constexpr std::uint8_t kOpGotoTop      = 0x40;

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

TEST_CASE("Teardown batching: clean triple costs nothing") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    const std::uint64_t fl0 = tb_op(kOpFlushFile);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t top0 = tb_op(kOpGotoTop);

    // Nothing was written, so the blind Flush sets no flag, the
    // CloseAll parks (bindings stay live), and the natural nav finds
    // a pristine server: the whole teardown triple costs nothing.
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpFlushFile) == fl0);
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpGotoTop) == top0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: dirty flush travels alone under a park") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // A buffered write makes the table dirty: the Flush flag sticks,
    // but the parked close is still adopted — one flush frame, no
    // close frame, then the nav.
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hTable, fld, 60.0) == AE_SUCCESS);
    const std::uint64_t fl0 = tb_op(kOpFlushFile);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t top0 = tb_op(kOpGotoTop);
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpFlushFile) == fl0 + 1);
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpGotoTop) == top0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: merged flush survives for unparked closes") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // Dirty table, CloseAll parked, then a structural change drops
    // the park with the flags still owed: the next op merges
    // flush-into-close (one CloseAll frame, zero flush frames).
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsSetDouble(hTable, fld, 60.0) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsFlushFileBuffers(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "TB.CDX";
    UNSIGNED8 tag[] = "BYID9";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    const std::uint64_t fl0 = tb_op(kOpFlushFile);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
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

TEST_CASE("Teardown batching: order key counts ride the parent cache") {    tb_wipe();
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
    CHECK(tb_op(kOpKeyCount) == kc0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: immutable metadata caches per handle") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // Table type + record length never change for an open handle:
    // one frame each no matter how often rddads asks per USE.
    const std::uint64_t tt0 = tb_op(0x6A);  // GetTableType
    const std::uint64_t rl0 = tb_op(0x6C);  // GetRecordLength
    UNSIGNED16 tt = 0;
    UNSIGNED32 rl = 0;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(AdsGetTableType(hTable, &tt) == AE_SUCCESS);
        REQUIRE(AdsGetRecordLength(hTable, &rl) == AE_SUCCESS);
    }
    CHECK(tt == 2u);  // ADS_CDX
    CHECK(rl > 0u);
    CHECK(tb_op(0x6A) == tt0 + 1);
    CHECK(tb_op(0x6C) == rl0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

// Index-binding park (Vouch per-tag rotation: CloseAll→OpenIndex per
// tag, 166 + 125 frames/startup). CloseAll parks the live maps with
// the server bindings still open; a same-bag reopen restores them
// with zero frames. Post-close answers in the window are unchanged.

UNSIGNED16 tb_numidx(ADSHANDLE hTable) {
    UNSIGNED16 n = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &n) == AE_SUCCESS);
    return n;
}

void tb_open_bag(ADSHANDLE hTable, const char* bag) {
    ADSHANDLE arr[64] = {0};
    UNSIGNED16 alen = 64;
    UNSIGNED8 b[128]{};
    std::memcpy(b, bag, std::strlen(bag));
    REQUIRE(AdsOpenIndex(hTable, b, arr, &alen) == AE_SUCCESS);
}

TEST_CASE("Index park: same-bag rotation costs zero frames") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);
    CHECK(tb_numidx(hTable) == 1u);

    // The rotation heartbeat: close-all then reopen the same bag.
    // No frame either way; the bindings never left the server.
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t oi0 = tb_op(kOpOpenIndex);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    CHECK(tb_numidx(hTable) == 0u);  // post-close window answers hold
    tb_open_bag(hTable, "TB.CDX");
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpOpenIndex) == oi0);
    CHECK(tb_numidx(hTable) == 1u);

    // Parked ids are live: order + count + seek all work with no
    // reopen. A stale id would fail 5000 here, not just cost frames.
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    const std::uint64_t top0 = tb_op(kOpGotoTop);
    const std::uint64_t so0 = tb_op(kOpSetOrder);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);      // fused, nothing plain
    CHECK(tb_op(kOpGotoTop) == top0 + 1);  // the rotation's one frame
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpOpenIndex) == oi0);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);

    // Second rotation round-trips the same way (park, reopen, order).
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    tb_open_bag(hTable, "TB.CDX");
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpOpenIndex) == oi0);
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Index park: repeated clears keep the snapshot") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // The RDD issues OrdListClear twice per rotation: the second
    // clear must not overwrite the parked snapshot with empty maps,
    // or every reopen misses and pays both frames (the v1.09.38
    // field shape — identical counts with the park loaded).
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t oi0 = tb_op(kOpOpenIndex);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    CHECK(tb_numidx(hTable) == 0u);
    tb_open_bag(hTable, "TB.CDX");
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpOpenIndex) == oi0);
    CHECK(tb_numidx(hTable) == 1u);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Index park: full rotation without reopen costs one frame") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // The field rotation shape (mus_d_file): Clear, Clear, resolve
    // the order, switch, position — with no OpenIndex in between.
    // Order resolution consults the parked snapshot (server-live
    // ids), the switch defers, the nav fuses: one GotoTop frame for
    // the whole rotation, zero open/close/order frames.
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t oi0 = tb_op(kOpOpenIndex);
    const std::uint64_t so0 = tb_op(kOpSetOrder);
    const std::uint64_t top0 = tb_op(kOpGotoTop);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);
    ADSHANDLE hOrd1 = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd1) == AE_SUCCESS);
    CHECK(hOrd1 == hOrd);
    REQUIRE(AdsSetIndexOrder(hTable, want) == AE_SUCCESS);
    CHECK(tb_op(kOpSetOrder) == so0);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpCloseAllIdx) == ca0);
    CHECK(tb_op(kOpOpenIndex) == oi0);
    CHECK(tb_op(kOpSetOrder) == so0);
    CHECK(tb_op(kOpGotoTop) == top0 + 1);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
TEST_CASE("Index park: table nav inside the window forces a real close") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // Install a server order first (fused switch, acked), then park:
    // the server still runs it while the client believes natural. A
    // contextless table nav must resolve the divergence with a real
    // close — adoption would walk the stale order while the client
    // accounts natural rows.
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t top0 = tb_op(kOpGotoTop);
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(tb_op(kOpCloseAllIdx) == ca0 + 1);
    CHECK(tb_op(kOpGotoTop) == top0 + 1);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 1u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Index park: CreateIndex invalidates the snapshot") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // Park, then structurally change the bag: the snapshot predates
    // the new tag, so the next rotation reopens for real (frames
    // flow) and the new tag is visible immediately after.
    REQUIRE(AdsCloseAllIndexes(hTable) == AE_SUCCESS);
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "TB.CDX";
    UNSIGNED8 tag[] = "BYID2";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    const std::uint64_t ca0 = tb_op(kOpCloseAllIdx);
    const std::uint64_t oi0 = tb_op(kOpOpenIndex);
    tb_open_bag(hTable, "TB.CDX");
    CHECK(tb_op(kOpCloseAllIdx) == ca0 + 1);
    CHECK(tb_op(kOpOpenIndex) == oi0 + 1);
    CHECK(tb_numidx(hTable) == 2u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Teardown batching: open bags answer existence locally") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv.set_enable_file_func(true);
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // The production bag is bound by this open table: existence is
    // trivially true (any spelling that stems the same, including
    // the .z01 production alias) with zero frames.
    const std::uint64_t fe0 = tb_op(kOpFileExists);
    UNSIGNED16 ex = 0;
    UNSIGNED8 bag1[] = "TB.CDX";
    REQUIRE(AdsCheckExistence(hConn, bag1, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    UNSIGNED8 bag2[] = "tb.cdx";
    REQUIRE(AdsCheckExistence(hConn, bag2, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    UNSIGNED8 bag3[] = "TB.z01";
    REQUIRE(AdsCheckExistence(hConn, bag3, &ex) == AE_SUCCESS);
    CHECK(ex == 1u);
    CHECK(tb_op(kOpFileExists) == fe0);

    // A genuinely missing file still goes to the wire (negatives are
    // never cached: the file may appear at any time).
    UNSIGNED8 missing[] = "NOPE.CDX";
    REQUIRE(AdsCheckExistence(hConn, missing, &ex) == AE_SUCCESS);
    CHECK(ex == 0u);
    CHECK(tb_op(kOpFileExists) == fe0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("KeyCount: rotation revisits ride the per-order map") {
    tb_wipe();
    auto dir = tb_tmp_dir();
    tb_seed(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = tb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = tb_open(hConn);

    // Second tag on the same bag for the revisit.
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "TB.CDX";
    UNSIGNED8 tag2[] = "BYID2";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag2, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    ADSHANDLE hOrd1 = 0;
    UNSIGNED8 want1[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hTable, want1, &hOrd1) == AE_SUCCESS);
    ADSHANDLE hOrd2 = 0;
    UNSIGNED8 want2[] = "BYID2";
    REQUIRE(AdsGetIndexHandle(hTable, want2, &hOrd2) == AE_SUCCESS);

    // Each switch certifies its order's count in the ack (or the
    // per-order map serves revisits): no GetKeyCount frame anywhere.
    const std::uint64_t kc0 = tb_op(kOpKeyCount);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd1) == AE_SUCCESS);
    REQUIRE(AdsGetKeyCount(hOrd1, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    CHECK(tb_op(kOpKeyCount) == kc0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd2) == AE_SUCCESS);
    REQUIRE(AdsGetKeyCount(hOrd2, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    CHECK(tb_op(kOpKeyCount) == kc0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd1) == AE_SUCCESS);
    REQUIRE(AdsGetKeyCount(hOrd1, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 5u);
    CHECK(tb_op(kOpKeyCount) == kc0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}
