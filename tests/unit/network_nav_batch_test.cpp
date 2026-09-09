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
constexpr std::uint8_t kOpGotoBottom = 0x64;

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

    // The wire AtBOF carries the EOF twin: the follow-up AtEOF is local.
    CHECK(nb_bof(hTable) == 0);  // arrived from rows, not BOF
    CHECK(nb_op(kOpAtBOF) == bof0 + 1);
    CHECK(nb_eof(hTable) == 1);
    CHECK(nb_op(kOpAtEOF) == eof0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    srv.stop();
}

TEST_CASE("Nav batching: order context defeats duplicate suppression") {
    nb_wipe();
    auto dir = nb_tmp_dir();

    // Physical IDs 30/10/20: natural top is rec 1, ordered top is rec 2.
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
