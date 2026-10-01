// tests/unit/network_empty_window_test.cpp
// Short "still empty" window (patch 2): after the server certifies a
// table physically empty, Seek / GO n are answered locally for up to
// OPENADS_EMPTY_TTL_MS (default 1500 ms). Own writes close the window
// immediately; after the window the wire is used again.
#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

std::uint64_t ew_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op]
        .count.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("Remote empty window: local not-found, closes on own write/TTL") {
    auto dir = fs::temp_directory_path() / "openads_emptywin";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0";
    UNSIGNED8 tn0[] = "EW.DBF";
    ADSHANDLE hT0 = 0;
    REQUIRE(AdsCreateTable(hC0, tn0, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
            == AE_SUCCESS);
    ADSHANDLE hI0 = 0;
    UNSIGNED8 bag[] = "EW.CDX";
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "STR(ID,8)";
    REQUIRE(AdsCreateIndex61(hT0, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI0) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
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

    UNSIGNED8 tn[] = "EW.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);   // server certifies empty

    UNSIGNED8 key[] = "       7";
    const auto sk0 = ew_op(0x90);
    const auto gr0 = ew_op(0x58);
    UNSIGNED16 found = 9;
    REQUIRE(AdsSeek(hOrd, key, 8, ADS_STRINGKEY, ADS_HARDSEEK, &found)
            == AE_SUCCESS);
    CHECK(found == 0);
    REQUIRE(AdsGotoRecord(hT, 1) == AE_SUCCESS);
    CHECK(ew_op(0x90) - sk0 == 0u);
    CHECK(ew_op(0x58) - gr0 == 0u);
    UNSIGNED16 b = 0, e = 0;
    REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
    CHECK(b == 1);
    CHECK(e == 1);

    SUBCASE("own append closes the window") {
        UNSIGNED8 f1[] = "ID";
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, f1, 7) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
        const auto sk1 = ew_op(0x90);
        found = 0;
        REQUIRE(AdsSeek(hOrd, key, 8, ADS_STRINGKEY, ADS_HARDSEEK, &found)
                == AE_SUCCESS);
        CHECK(ew_op(0x90) - sk1 == 1u);
        CHECK(found == 1);
    }

    SUBCASE("window ends after the TTL") {
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        const auto sk1 = ew_op(0x90);
        found = 9;
        REQUIRE(AdsSeek(hOrd, key, 8, ADS_STRINGKEY, ADS_HARDSEEK, &found)
                == AE_SUCCESS);
        CHECK(ew_op(0x90) - sk1 == 1u);
        CHECK(found == 0);
    }

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}
