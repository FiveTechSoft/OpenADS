// tests/unit/network_boundary_pair_test.cpp
// mtfix12 R1/R2/R3 — boundary-pair certification, self-GotoRecord
// suppression, and GetRecordNum anchoring, end to end over a loopback
// server: per-opcode frame counts prove which calls hit the wire, and
// reads prove the locally-served state is what the wire would have
// returned.
#include "doctest.h"
#include "network/client.h"
#include "network/server.h"
#include "openads/ace.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::atomic<unsigned> g_top{0}, g_bottom{0}, g_goto{0}, g_recnum{0}, g_keycount{0};

void count_nav(const void*, std::uint8_t op, std::uint32_t, std::size_t,
               std::uint8_t, std::size_t, long long) {
    if (op == 0x40) g_top.fetch_add(1);          // GotoTop
    if (op == 0x64) g_bottom.fetch_add(1);       // GotoBottom
    if (op == 0x58) g_goto.fetch_add(1);         // GotoRecord
    if (op == 0x4E) g_recnum.fetch_add(1);
    if (op == 0xB0) g_keycount.fetch_add(1);       // GetKeyCount
}

void reset_counts() {
    g_top = 0; g_bottom = 0; g_goto = 0; g_recnum = 0; g_keycount = 0;
}

std::uint32_t recno_of(ADSHANDLE h) {
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordNum(h, 0, &n) == AE_SUCCESS);
    return n;
}

std::string id_field(ADSHANDLE h) {
    char buf[64] = {};
    UNSIGNED32 len = sizeof(buf) - 1;
    UNSIGNED8 fld[] = "ID";
    REQUIRE(AdsGetField(h, fld, reinterpret_cast<UNSIGNED8*>(buf), &len,
                        ADS_NONE) == AE_SUCCESS);
    return std::string(buf, len);
}

} // namespace

TEST_CASE("boundary pair, self-goto, and recno anchor over loopback") {
    auto dir = fs::temp_directory_path() / "openads_bpair";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Seed two 5-row tables locally.
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0";
    for (const char* nm : {"BP1.DBF", "BP2.DBF"}) {
        UNSIGNED8 tn[64]{};
        std::memcpy(tn, nm, std::strlen(nm) + 1);
        ADSHANDLE h = 0;
        REQUIRE(AdsCreateTable(hC0, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def,
                               &h) == AE_SUCCESS);
        for (int i = 1; i <= 5; ++i) {
            REQUIRE(AdsAppendRecord(h) == AE_SUCCESS);
            char v[16];
            std::snprintf(v, sizeof(v), "%d", i);
            UNSIGNED8 fld[] = "ID";
            REQUIRE(AdsSetField(h, fld,
                                reinterpret_cast<UNSIGNED8*>(v),
                                static_cast<UNSIGNED32>(std::strlen(v)))
                    == AE_SUCCESS);
        }
        REQUIRE(AdsCloseTable(h) == AE_SUCCESS);
    }
    REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);

    UNSIGNED8 t1[] = "BP1.DBF";
    UNSIGNED8 t2[] = "BP2.DBF";
    ADSHANDLE hT = 0, hU = 0;
    REQUIRE(AdsOpenTable(hC, t1, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsOpenTable(hC, t2, nullptr, ADS_CDX, 0, 0, 0, 0, &hU)
            == AE_SUCCESS);

    openads::network::set_frame_trace_hook(&count_nav);

    // Settle: position somewhere known; whatever it costs is not counted.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hU) == AE_SUCCESS);
    reset_counts();

    // R2 — self-GotoRecord: first GO wires and certifies the anchor,
    // the repeat is served locally.
    REQUIRE(AdsGotoRecord(hT, 3) == AE_SUCCESS);
    CHECK(g_goto.load() == 1u);
    CHECK(recno_of(hT) == 3u);
    CHECK(g_recnum.load() == 0u);          // R3: from the anchor
    REQUIRE(AdsGotoRecord(hT, 3) == AE_SUCCESS);
    CHECK(g_goto.load() == 1u);            // no new frame
    CHECK(recno_of(hT) == 3u);

    // R1 — the GotoTop ack certified the bottom in the same visit.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 1u);
    CHECK(recno_of(hT) == 1u);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 0u);          // served from the pair blob
    CHECK(recno_of(hT) == 5u);             // and it IS the bottom row
    CHECK(id_field(hT) == "5");
    UNSIGNED16 atEof = 1;
    REQUIRE(AdsAtEOF(hT, &atEof) == AE_SUCCESS);
    CHECK(atEof == 0);

    // The pair serve stamped bottom; a consecutive bottom is the old
    // duplicate path, and the top that follows re-wires (no fresh
    // certification for top was made by the LOCAL bottom serve... the
    // certification from the earlier GotoBottom... none happened, so
    // this top must wire and re-certify bottom).
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 0u);          // duplicate of the pair serve
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 2u);             // wire: re-certifies bottom
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 0u);          // pair again
    CHECK(recno_of(hT) == 5u);

    // A write on this table kills the certification (the blob's row
    // bytes predate it) and the frame expires the conn-wide seq anyway.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 3u);             // wire: certify bottom again
    REQUIRE(AdsLockRecord(hT, 0) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    UNSIGNED8 seven[] = "7";
    REQUIRE(AdsSetField(hT, fld, seven, 1) == AE_SUCCESS);
    REQUIRE(AdsUnlockRecord(hT, 0) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 1u);          // back on the wire
    CHECK(recno_of(hT) == 5u);
    CHECK(id_field(hT) == "5");            // row 5 untouched by the write

    // The wire GotoBottom also certified the TOP: this GotoTop is
    // itself pair-served (symmetric certification).
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 3u);             // pair-served from the bottom
    CHECK(recno_of(hT) == 1u);
    // Conn-wide envelope: nav on ANOTHER table expires the pair.
    REQUIRE(AdsGotoRecord(hU, 2) == AE_SUCCESS);   // other table's nav
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 2u);          // wire: conn seq moved

    openads::network::set_frame_trace_hook(nullptr);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hU) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("boundary pair: scoped ordered bottom counts on demand") {
    auto dir = fs::temp_directory_path() / "openads_bpair_scoped";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    UNSIGNED8 path[512]{};
    std::memcpy(path, dir.string().c_str(), dir.string().size());
    ADSHANDLE local = 0;
    REQUIRE(AdsConnect60(path, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &local) == AE_SUCCESS);
    UNSIGNED8 tn[] = "SCOPE.DBF";
    UNSIGNED8 def[] = "GRP,C,1,0;ID,C,2,0";
    ADSHANDLE tbl = 0;
    REQUIRE(AdsCreateTable(local, tn, nullptr, ADS_CDX, 0, 0, 0, 0,
                           def, &tbl) == AE_SUCCESS);
    UNSIGNED8 grp[] = "GRP", id[] = "ID";
    for (const char* value : {"A1", "A2", "A3", "B4"}) {
        REQUIRE(AdsAppendRecord(tbl) == AE_SUCCESS);
        REQUIRE(AdsSetField(tbl, grp, (UNSIGNED8*)value, 1) == AE_SUCCESS);
        REQUIRE(AdsSetField(tbl, id, (UNSIGNED8*)(value + 1), 1)
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(tbl) == AE_SUCCESS);
    }
    // One deleted A row makes physical count (4), scoped count (3)
    // and scoped SET DELETED count (2) distinct.
    REQUIRE(AdsGotoRecord(tbl, 2) == AE_SUCCESS);
    REQUIRE(AdsDeleteRecord(tbl) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(tbl) == AE_SUCCESS);
    UNSIGNED8 bag[] = "SCOPE.CDX", tag[] = "BYGRP", expr[] = "GRP+ID";
    ADSHANDLE idx = 0;
    REQUIRE(AdsCreateIndex61(tbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &idx) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(tbl) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(local) == AE_SUCCESS);

    openads::network::Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(server.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE conn = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &conn) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(conn, tn, nullptr, ADS_CDX, 0, 0, 0, 0,
                         &tbl) == AE_SUCCESS);
    REQUIRE(AdsGetIndexHandleByOrder(tbl, 1, &idx) == AE_SUCCESS);
    REQUIRE(idx != 0);
    UNSIGNED8 a[] = "A";
    REQUIRE(AdsSetScope(idx, ADS_TOP, a, 1, ADS_STRINGKEY) == AE_SUCCESS);
    REQUIRE(AdsSetScope(idx, ADS_BOTTOM, a, 1, ADS_STRINGKEY) == AE_SUCCESS);
    REQUIRE(AdsShowDeleted(0) == AE_SUCCESS);
    openads::network::set_frame_trace_hook(&count_nav);
    reset_counts();
    REQUIRE(AdsGotoTop(idx) == AE_SUCCESS);
    CHECK(recno_of(tbl) == 1u);
    CHECK(g_keycount.load() == 0u);  // no eager count from a plain GoTop
    REQUIRE(AdsGotoBottom(idx) == AE_SUCCESS);
    CHECK(g_bottom.load() == 0u);    // opposite landing served from pair
    CHECK(recno_of(tbl) == 3u);      // deleted row 2 skipped
    // A cold scoped order count is fetched only when Bottom is consumed.
    CHECK(g_keycount.load() == 1u);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(idx, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 2u);                 // not physical 4 or scoped 3
    CHECK(g_keycount.load() == 1u);  // cached after bottom
    REQUIRE(AdsGotoTop(idx) == AE_SUCCESS);
    CHECK(recno_of(tbl) == 1u);

    // Scope to no rows; top and bottom agree on BOF+EOF without
    // treating a physical row or deleted key as visible.
    UNSIGNED8 z[] = "Z";
    REQUIRE(AdsSetScope(idx, ADS_TOP, z, 1, ADS_STRINGKEY) == AE_SUCCESS);
    REQUIRE(AdsSetScope(idx, ADS_BOTTOM, z, 1, ADS_STRINGKEY) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(idx) == AE_SUCCESS);
    UNSIGNED16 b = 0, e = 0;
    REQUIRE(AdsAtBOF(tbl, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(tbl, &e) == AE_SUCCESS);
    CHECK(b == 1); CHECK(e == 1);
    REQUIRE(AdsGotoBottom(idx) == AE_SUCCESS);
    REQUIRE(AdsAtBOF(tbl, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(tbl, &e) == AE_SUCCESS);
    CHECK(b == 1); CHECK(e == 1);
    REQUIRE(AdsGetKeyCount(idx, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 0u);
    openads::network::set_frame_trace_hook(nullptr);
    REQUIRE(AdsShowDeleted(1) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(tbl) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(conn) == AE_SUCCESS);
    server.stop();
    fs::remove_all(dir, ec);
}
