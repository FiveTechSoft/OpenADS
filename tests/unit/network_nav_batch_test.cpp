// tests/unit/network_nav_batch_test.cpp
// Nav-probe batching (Vouch startup: ~33 wire RTTs per USE on bare
// boundary probing). Three kills, all proven by server opcode counters:
//
//  A. Consecutive-duplicate GotoTop/GotoBottom skip their frame when
//     nothing hit the wire since (identical state, provably).
//  B. A top/bottom that produced no row proves an empty cursor, so
//     AtBOF/AtEOF answer locally until anything touches the wire.
//  C. AtBOF/AtEOFAck carry the twin flag ([u8 bof][u8 eof] /
//     [u8 eof][u8 bof]), so the wire half of rddads' pair caches the
//     twin answer and the other half is served locally.
//
// Duplicate suppression is order-aware: a top in order A says nothing
// about order B, so the stamp carries the order context (RemoteIndex
// id, or the ack-confirmed server binding for table-handle nav).

#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr std::uint8_t kOpGotoTop    = 0x40;
constexpr std::uint8_t kOpAtEOF      = 0x48;
constexpr std::uint8_t kOpAtBOF      = 0x4C;
constexpr std::uint8_t kOpGetRecordNum = 0x4E;
constexpr std::uint8_t kOpGotoRecord = 0x58;
constexpr std::uint8_t kOpGotoBottom = 0x64;
constexpr std::uint8_t kOpSetOrder   = 0x8C;
constexpr std::uint8_t kOpSeek       = 0x90;
constexpr std::uint8_t kOpKeyCount   = 0xB0;
constexpr std::uint8_t kOpCloseTable = 0x22;
constexpr std::uint8_t kOpGetRecordCount = 0x46;

fs::path nb_tmp_dir() {
    return fs::temp_directory_path() / "openads_navbatch_test";
}

void nb_wipe() {
    std::error_code ec;
    fs::remove_all(nb_tmp_dir(), ec);
    fs::create_directories(nb_tmp_dir(), ec);
}

void nb_seed(const fs::path& dir, const char* tname, int rows) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "NM,C,10,0";
    UNSIGNED8 tn[64]{};
    std::memcpy(tn, tname, std::strlen(tname));
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tn, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);
    UNSIGNED8 fld[] = "NM";
    for (int i = 0; i < rows; ++i) {
        char v[16]{};
        std::snprintf(v, sizeof(v), "r%d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, fld,
                             reinterpret_cast<UNSIGNED8*>(v),
                             static_cast<UNSIGNED32>(std::strlen(v)))
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::uint64_t nb_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op].count.load(std::memory_order_relaxed);
}

ADSHANDLE nb_connect_remote(const fs::path& dir, std::uint16_t port) {
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

ADSHANDLE nb_open(ADSHANDLE hConn, const char* tname) {
    UNSIGNED8 tn[64]{};
    std::memcpy(tn, tname, std::strlen(tname));
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tn, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);
    return hTable;
}

UNSIGNED16 nb_bof(ADSHANDLE hTable) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtBOF(hTable, &v) == AE_SUCCESS);
    return v;
}

UNSIGNED16 nb_eof(ADSHANDLE hTable) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtEOF(hTable, &v) == AE_SUCCESS);
    return v;
}

UNSIGNED32 nb_recno(ADSHANDLE hTable) {
    UNSIGNED32 v = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &v) == AE_SUCCESS);
    return v;
}

// Ordered fixture: physical IDs 30/10/20, tag BYID on ID.
// Natural top is rec 1, ordered top is rec 2.
void nb_seed_ord(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn0)
            == AE_SUCCESS);
    UNSIGNED8 def[]   = "ID,N,8,0";
    UNSIGNED8 tname[] = "ord.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn0, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    const double ids[] = {30.0, 10.0, 20.0};
    for (double id : ids) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, fld, id) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "ord.cdx";
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn0) == AE_SUCCESS);
}

} // namespace

TEST_CASE("Nav batching: duplicate GotoTop/GotoBottom skip their frame") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "nb.dbf", 3);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = nb_open(hConn, "nb.dbf");

    const std::uint64_t top0 = nb_op(kOpGotoTop);
    const std::uint64_t bot0 = nb_op(kOpGotoBottom);

    // Warm open already positioned at top: immediate GoTop probing
    // (rddads' per-USE pattern) costs nothing.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0);

    // Bottom twice: one frame, then suppressed.
    REQUIRE(AdsGotoBottom(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoBottom) == bot0 + 1);

    // A wire nav in between invalidates: top goes out again.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Nav batching: empty table answers boundaries with zero frames") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "empty.dbf", 0);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = nb_open(hConn, "empty.dbf");

    const std::uint64_t top0 = nb_op(kOpGotoTop);
    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);

    // Warm open stamped the empty cursor: top + every BOF/EOF probe
    // below is served locally. An empty cursor is both BOF and EOF.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    for (int i = 0; i < 3; ++i) {
        CHECK(nb_bof(hTable) == 1);
        CHECK(nb_eof(hTable) == 1);
    }
    CHECK(nb_op(kOpGotoTop) == top0);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);

    // Break the stamp with a local filter reset: the next top really
    // goes out (empty again), then boundaries go quiet again.
    REQUIRE(AdsClearFilter(hTable) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Nav batching: twin flag halves the BOF/EOF pair") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "two.dbf", 2);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, srv.port());
    ADSHANDLE hTable = nb_open(hConn, "two.dbf");

    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);  // past the end -> EOF

    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);

    // Arrived from rows, so not-BOF is proven: both answers local, no
    // wire AtBOF at all (the twin would halve the pair if one went out).
    CHECK(nb_bof(hTable) == 0);  // arrived from rows, not BOF
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_op(kOpAtEOF) == eof0);

    // Force a wire pair: a local filter change drops the proven-false
    // flags without moving the server cursor (still past end). The
    // wire AtBOF carries the EOF twin, so the follow-up AtEOF is local.
    UNSIGNED8 flt[] = "ID > 0";
    REQUIRE(AdsSetFilter(hTable, flt) == AE_SUCCESS);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_op(kOpAtBOF) == bof0 + 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_op(kOpAtEOF) == eof0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Nav batching: order context defeats duplicate suppression") {    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_ord(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "ord.dbf");

    // Resolve the order handle (purely local) then navigate by it: the
    // top MUST go out on the wire in BYID order (rec 2), not dedupe
    // against the warm natural-order stamp (which would sit on rec 1).
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    const std::uint64_t top0 = nb_op(kOpGotoTop);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 2u);

    // Same order again: now the duplicate is real — suppressed.
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);

    // Table-handle top with the order still bound: same position,
    // suppressed as well.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);

    // Back to natural order: reset frame + fresh top both go out.
    REQUIRE(AdsSetIndexOrderByHandle(hTable, 0) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoTop) == top0 + 2);
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 1u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Nav batching: stamps survive read traffic") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "rdempty.dbf", 0);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "rdempty.dbf");

    // rddads interleaves field/count reads between probes. Reads must
    // not expire the empty-cursor stamp: top, a wire count read, then
    // boundaries — still zero boundary frames.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    UNSIGNED32 nrec = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &nrec) == AE_SUCCESS);
    CHECK(nrec == 0u);

    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Nav batching: skip-established limits answer locally") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "lim.dbf", 2);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "lim.dbf");

    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    // Past the end: EOF is flagged, and arriving from rows proves
    // not-BOF — both answer with zero further frames.
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);

    // Back to the top limit: BOF flags locally, and the Skip ack's
    // bound piggyback certifies EOF too — zero frames either side.
    REQUIRE(AdsSkip(hTable, -10) == AE_SUCCESS);
    const std::uint64_t bof1 = nb_op(kOpAtBOF);
    const std::uint64_t eof1 = nb_op(kOpAtEOF);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_op(kOpAtBOF) == bof1);
    CHECK(nb_op(kOpAtEOF) == eof1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Reposition truth: top/bottom/skip certify bounds and recno") {    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "tbs.dbf", 3);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "tbs.dbf");

    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    const std::uint64_t rn0 = nb_op(kOpGetRecordNum);

    // Top lands on row 1: positioned (not BOF — BOF means *before*
    // first), not EOF, recno certified — the paint burst is local.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_recno(hTable) == 1u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    // Bottom lands on the last row: same, mirrored.
    REQUIRE(AdsGotoBottom(hTable) == AE_SUCCESS);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_recno(hTable) == 3u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    // Skip past the end: EOF phantom with recno n+1, still certified.
    REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_recno(hTable) == 4u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Nav batching: deferred SetOrder fuses into GotoTop") {    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_ord(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "ord.dbf");

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    // SetOrder alone sends nothing; the following GotoTop absorbs it:
    // one GotoTop frame, zero SetOrder frames, ordered position.
    const std::uint64_t so0 = nb_op(kOpSetOrder);
    const std::uint64_t top0 = nb_op(kOpGotoTop);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    CHECK(nb_op(kOpGotoTop) == top0 + 1);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 2u);

    // Same pair to the bottom: fused the same way.
    const std::uint64_t bot0 = nb_op(kOpGotoBottom);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    CHECK(nb_op(kOpGotoBottom) == bot0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Nav batching: close absorbs a deferred switch") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_ord(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "ord.dbf");

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    // Deferred switch that never reaches any wire op dies silently
    // with the close (bindings die with the handle).
    const std::uint64_t so0 = nb_op(kOpSetOrder);
    const std::uint64_t cl0 = nb_op(kOpCloseTable);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    CHECK(nb_op(kOpCloseTable) == cl0 + 1);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Nav batching: non-nav op flushes a deferred switch plainly") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_ord(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "ord.dbf");

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    // A key count needs the binding: the deferred switch goes out
    // plainly first, then the count.
    const std::uint64_t so0 = nb_op(kOpSetOrder);
    const std::uint64_t kc0 = nb_op(kOpKeyCount);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == AE_SUCCESS);
    CHECK(nb_op(kOpSetOrder) == so0);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 3u);
    CHECK(nb_op(kOpSetOrder) == so0 + 1);
    CHECK(nb_op(kOpKeyCount) == kc0 + 1);
    // And again, cached this time.
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 3u);
    CHECK(nb_op(kOpKeyCount) == kc0 + 1);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

// Reposition-bound piggyback (Vouch paint loop: GotoRecord/Seek, then
// AtBOF/AtEOF/RecNo per row — 266 + 174 repositions/startup, 548 RecNo
// + 352 AtBOF wire frames). The server appends [u8 bof][u8 eof][u32 recno]
// after the row trailer of GotoRecordAck/SeekAck, so the whole
// post-reposition burst is served locally. Trailing section,
// length-gated: old servers send no tail (M12.24 convention, no caps bit).

TEST_CASE("Reposition truth: GotoRecord certifies bounds and recno") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "gr.dbf", 3);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "gr.dbf");

    const std::uint64_t gr0 = nb_op(kOpGotoRecord);
    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    const std::uint64_t rn0 = nb_op(kOpGetRecordNum);

    // Mid-table restore: one frame, then the full paint burst is local.
    REQUIRE(AdsGotoRecord(hTable, 2) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoRecord) == gr0 + 1);
    for (int i = 0; i < 3; ++i) {
        CHECK(nb_bof(hTable) == 0);
        CHECK(nb_eof(hTable) == 0);
        CHECK(nb_recno(hTable) == 2u);
    }
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    // Past-the-end restore: Clipper-phantom Limbo (BOF+EOF, recno
    // LastRec()+1) — certified in the ack, still zero frames.
    REQUIRE(AdsGotoRecord(hTable, 4) == AE_SUCCESS);
    CHECK(nb_op(kOpGotoRecord) == gr0 + 2);
    CHECK(nb_bof(hTable) == 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_recno(hTable) == 4u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

// Character-tag fixture for seeks: rows r0/r1/r2, tag BYNM on NM.
void nb_seed_chr(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn0)
            == AE_SUCCESS);
    UNSIGNED8 def[]   = "NM,C,10,0";
    UNSIGNED8 tname[] = "chr.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn0, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                           &hT) == AE_SUCCESS);
    UNSIGNED8 fld[] = "NM";
    for (int i = 0; i < 3; ++i) {
        char v[16]{};
        std::snprintf(v, sizeof(v), "r%d", i);
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetString(hT, fld, reinterpret_cast<UNSIGNED8*>(v),
                             static_cast<UNSIGNED32>(std::strlen(v)))
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    ADSHANDLE hI = 0;
    UNSIGNED8 bag[] = "chr.cdx";
    UNSIGNED8 tag[] = "BYNM";
    UNSIGNED8 exp[] = "NM";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn0) == AE_SUCCESS);
}

UNSIGNED16 nb_seek(ADSHANDLE hOrd, const char* key) {
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hOrd, reinterpret_cast<UNSIGNED8*>(
                        const_cast<char*>(key)),
                    static_cast<UNSIGNED16>(std::strlen(key)),
                    ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
    return found;
}

TEST_CASE("Reposition truth: Seek hit certifies bounds and recno") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_chr(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "chr.dbf");
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    const std::uint64_t sk0 = nb_op(kOpSeek);
    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    const std::uint64_t rn0 = nb_op(kOpGetRecordNum);

    // Mid-key hit: one Seek frame, burst local.
    CHECK(nb_seek(hOrd, "r1") == 1);
    CHECK(nb_op(kOpSeek) == sk0 + 1);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_recno(hTable) == 2u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    // First-key hit: positioned on a row, so BOF is false (ACE truth:
    // BOF means positioned *before* first, not *on* first). The point
    // stands: the piggyback — not a follow-up frame — certifies it.
    CHECK(nb_seek(hOrd, "r0") == 1);
    CHECK(nb_op(kOpSeek) == sk0 + 2);
    CHECK(nb_bof(hTable) == 0);
    CHECK(nb_eof(hTable) == 0);
    CHECK(nb_recno(hTable) == 1u);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Reposition truth: Seek miss certifies EOF locally") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed_chr(dir);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "chr.dbf");
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == AE_SUCCESS);
    REQUIRE(hOrd != 0);

    const std::uint64_t sk0 = nb_op(kOpSeek);
    const std::uint64_t bof0 = nb_op(kOpAtBOF);
    const std::uint64_t eof0 = nb_op(kOpAtEOF);
    const std::uint64_t rn0 = nb_op(kOpGetRecordNum);

    // Hard miss past the end: the cursor is in Limbo (BOF+EOF, the
    // Clipper-phantom convention) — certified in the ack, all local.
    // Value-identical to the old wire poll (the server twin is the
    // same ACE engine either way); only the frame is gone.
    CHECK(nb_seek(hOrd, "zzz") == 0);
    CHECK(nb_op(kOpSeek) == sk0 + 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_bof(hTable) == 1);
    // Certified recno, whatever the twin reports for a miss — served
    // locally and stable across repeats.
    CHECK(nb_recno(hTable) == nb_recno(hTable));
    CHECK(nb_op(kOpGetRecordNum) == rn0);
    CHECK(nb_op(kOpAtBOF) == bof0);
    CHECK(nb_op(kOpAtEOF) == eof0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

UNSIGNED32 nb_reccount(ADSHANDLE hTable) {
    UNSIGNED32 v = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &v) == AE_SUCCESS);
    return v;
}

TEST_CASE("Count truth: nav ack certifies the record count") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "cnt.dbf", 3);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hTable = nb_open(hConn, "cnt.dbf");

    // The USE flow (open, position, count) pays no count frame: the
    // nav ack certified it. (Bottom, not top: the open's implicit
    // GoTop dedupes a repeat top with no ack and no tail.)
    const std::uint64_t rc0 = nb_op(kOpGetRecordCount);
    REQUIRE(AdsGotoBottom(hTable) == AE_SUCCESS);
    CHECK(nb_reccount(hTable) == 3u);
    CHECK(nb_reccount(hTable) == 3u);
    CHECK(nb_op(kOpGetRecordCount) == rc0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}

TEST_CASE("Phantom RecNo derives from flags plus cached count") {
    nb_wipe();
    auto dir = nb_tmp_dir();
    nb_seed(dir, "ph1.dbf", 3);
    nb_seed(dir, "ph2.dbf", 3);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = nb_connect_remote(dir, s.port());
    ADSHANDLE hA = nb_open(hConn, "ph1.dbf");
    ADSHANDLE hB = nb_open(hConn, "ph2.dbf");

    // Park A at its EOF phantom (certified), cache its count, then
    // navigate B: the cross-table frame evicts A's seq-gated caches,
    // but the phantom recno re-derives (EOF + count) with no frame.
    REQUIRE(AdsGotoBottom(hA) == AE_SUCCESS);
    REQUIRE(AdsSkip(hA, 1) == AE_SUCCESS);
    CHECK(nb_reccount(hA) == 3u);
    const std::uint64_t rn0 = nb_op(kOpGetRecordNum);
    REQUIRE(AdsGotoTop(hB) == AE_SUCCESS);
    CHECK(nb_recno(hA) == 4u);
    CHECK(nb_recno(hA) == 4u);
    CHECK(nb_op(kOpGetRecordNum) == rn0);

    REQUIRE(AdsCloseTable(hA) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hB) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    s.stop();
}
