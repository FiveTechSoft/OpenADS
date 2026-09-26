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

std::atomic<unsigned> g_top{0}, g_bottom{0}, g_goto{0}, g_recnum{0};

void count_nav(const void*, std::uint8_t op, std::uint32_t, std::size_t,
               std::uint8_t, std::size_t, long long) {
    if (op == 0x40) g_top.fetch_add(1);          // GotoTop
    if (op == 0x64) g_bottom.fetch_add(1);       // GotoBottom
    if (op == 0x58) g_goto.fetch_add(1);         // GotoRecord
    if (op == 0x4E) g_recnum.fetch_add(1);       // GetRecordNum
}

void reset_counts() {
    g_top = 0; g_bottom = 0; g_goto = 0; g_recnum = 0;
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

    // mtfix14 diagnostic: no pair is negotiated. Each opposite-boundary
    // navigation goes over the wire; duplicate same-boundary calls remain
    // suppressed. Verify the record and field, not just frame counts.
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 1u);
    CHECK(recno_of(hT) == 1u);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 1u);
    CHECK(recno_of(hT) == 5u);
    CHECK(id_field(hT) == "5");
    UNSIGNED16 atEof = 1;
    REQUIRE(AdsAtEOF(hT, &atEof) == AE_SUCCESS);
    CHECK(atEof == 0);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 1u);  // duplicate
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 2u);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 2u);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 3u);
    REQUIRE(AdsLockRecord(hT, 0) == AE_SUCCESS);
    UNSIGNED8 fld[] = "ID";
    UNSIGNED8 seven[] = "7";
    REQUIRE(AdsSetField(hT, fld, seven, 1) == AE_SUCCESS);
    REQUIRE(AdsUnlockRecord(hT, 0) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 3u);
    CHECK(recno_of(hT) == 5u);
    CHECK(id_field(hT) == "5");
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(g_top.load() == 4u);
    CHECK(recno_of(hT) == 1u);
    REQUIRE(AdsGotoRecord(hU, 2) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    CHECK(g_bottom.load() == 4u);

    openads::network::set_frame_trace_hook(nullptr);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hU) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}
