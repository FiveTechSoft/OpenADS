// tests/unit/network_use_budget_test.cpp
// Per-USE wire budget (Vouch WAN startup). One realistic rddads-style
// USE cycle — open + setorder + gotop×2 + bof/eof pairs + bottom×2 +
// keycount + close — must cost a bounded number of server frames, with
// zero boundary-probe frames (sticky/twin) and zero duplicate navs:
//   Hello + Connect + OpenTable + CloseTable + GotoTop + GotoBottom
//   + SetOrder + KeyCount×2 = 10 frames total.
#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

std::uint64_t ub_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op]
        .count.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("Nav batching: full USE cycle stays within wire budget") {
    auto dir = fs::temp_directory_path() / "openads_usebudget";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0";
    UNSIGNED8 tn0[] = "UB.DBF";
    ADSHANDLE hT0 = 0;
    REQUIRE(AdsCreateTable(hC0, tn0, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
            == AE_SUCCESS);
    UNSIGNED8 f1[] = "ID";
    for (int i = 1; i <= 50; ++i) {
        REQUIRE(AdsAppendRecord(hT0) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT0, f1, i * 10) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT0) == AE_SUCCESS);
    }
    ADSHANDLE hI0 = 0;
    UNSIGNED8 bag[] = "UB.CDX";
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT0, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI0) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());

    std::map<std::uint8_t, std::uint64_t> before;
    for (int o = 0; o < 256; ++o)
        before[static_cast<std::uint8_t>(o)] = ub_op((std::uint8_t)o);

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);
    UNSIGNED8 tn[] = "UB.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT) == AE_SUCCESS);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    UNSIGNED16 b = 0, e = 0;
    REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
    REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 50u);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);

    auto delta = [&](std::uint8_t op) { return ub_op(op) - before[op]; };
    std::uint64_t total = 0;
    for (int o = 0; o < 256; ++o)
        total += ub_op((std::uint8_t)o) - before[(std::uint8_t)o];

    // The whole cycle, connect included, fits in a small fixed budget —
    // boundary probes and duplicate navs contribute zero frames.
    CHECK(total <= 14u);
    CHECK(delta(0x40) == 1u);  // GotoTop: second suppressed
    CHECK(delta(0x64) == 1u);  // GotoBottom: second suppressed
    CHECK(delta(0x48) == 0u);  // AtEOF: served locally
    CHECK(delta(0x4C) == 0u);  // AtBOF: served locally

    s.stop();
    fs::remove_all(dir, ec);
}
