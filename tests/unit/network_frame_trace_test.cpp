// tests/unit/network_frame_trace_test.cpp
// wire_trace diagnostics: the optional per-frame hook sees every
// completed round trip while installed, and nothing once removed.
// Default (no hook) is the production path.
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

std::atomic<unsigned> g_frames{0};
std::atomic<unsigned> g_open_frames{0};
std::atomic<unsigned> g_open_acks{0};

void count_frame(const void* conn, std::uint8_t op, std::uint32_t /*tid*/,
                 std::size_t /*req_bytes*/, std::uint8_t rep_op,
                 std::size_t /*rep_bytes*/, long long us) {
    CHECK(conn != nullptr);
    CHECK(us >= 0);
    g_frames.fetch_add(1);
    if (op == 0x20) {
        g_open_frames.fetch_add(1);
        if (rep_op == 0x21) g_open_acks.fetch_add(1);
    }
}

} // namespace

TEST_CASE("wire trace: frame hook counts round trips only while installed") {
    auto dir = fs::temp_directory_path() / "openads_frametrace";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Seed one table locally.
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0";
    UNSIGNED8 tn0[] = "FT.DBF";
    ADSHANDLE hT0 = 0;
    REQUIRE(AdsCreateTable(hC0, tn0, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
            == AE_SUCCESS);
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

    openads::network::set_frame_trace_hook(&count_frame);
    UNSIGNED8 tn[] = "FT.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    openads::network::set_frame_trace_hook(nullptr);

    CHECK(g_frames.load() >= 1u);
    CHECK(g_open_frames.load() == 1u);
    CHECK(g_open_acks.load() == 1u);

    // Removed: further traffic is not reported.
    const unsigned seen = g_frames.load();
    REQUIRE(AdsGotoBottom(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    CHECK(g_frames.load() == seen);

    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);
    s.stop();
    fs::remove_all(dir, ec);
}
