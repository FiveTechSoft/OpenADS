// tests/unit/network_version_test.cpp
// Server/DLL version reporting:
//   - AdsGetServerVersion(hRemoteConn) returns the serverd build behind
//     the connection (HelloAck handshake captured at connect time).
//   - AdsGetServerVersion(hLocalConn) returns the DLL's own build.
//   - OADS_ADSVERSION-style display (AdsGetVersion desc token) agrees
//     with both when client and server are the same binary (in-process
//     Server here), and carries the full dotted version ("1.09.27"),
//     not the SAP-shaped "1.9a" which drops the patch.

#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path nv_tmp_dir() {
    return fs::temp_directory_path() / "openads_version_test";
}

void nv_wipe() {
    std::error_code ec;
    fs::remove_all(nv_tmp_dir(), ec);
    fs::create_directories(nv_tmp_dir(), ec);
}

ADSHANDLE nv_connect_local(const fs::path& dir) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    return hConn;
}

ADSHANDLE nv_connect_remote(const fs::path& dir, std::uint16_t port) {
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

std::string nv_server_version(ADSHANDLE hConn) {
    char buf[64]{};
    UNSIGNED16 len = sizeof(buf);
    REQUIRE(AdsGetServerVersion(hConn,
                                reinterpret_cast<UNSIGNED8*>(buf),
                                &len) == AE_SUCCESS);
    CHECK(len > 0);
    CHECK_MESSAGE(len < sizeof(buf), "version truncated: ", buf);
    return std::string(buf);
}

// Full dotted build from the AdsGetVersion description
// ("OpenADS 1.09.27 ACE-compatible engine" -> "1.09.27").
std::string nv_dll_version() {
    UNSIGNED32 maj = 0, min = 0;
    UNSIGNED8 letter = 0;
    char desc[128]{};
    UNSIGNED16 len = sizeof(desc);
    REQUIRE(AdsGetVersion(&maj, &min, &letter,
                          reinterpret_cast<UNSIGNED8*>(desc),
                          &len) == AE_SUCCESS);
    std::string d(desc);
    const std::string pre = "OpenADS ";
    REQUIRE(d.compare(0, pre.size(), pre) == 0);
    auto end = d.find(' ', pre.size());
    REQUIRE(end != std::string::npos);
    return d.substr(pre.size(), end - pre.size());
}

} // namespace

TEST_CASE("Server version: remote reports server build, local reports DLL build") {
    nv_wipe();
    auto dir = nv_tmp_dir();

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    ADSHANDLE hLocal = nv_connect_local(dir);
    ADSHANDLE hRemote = nv_connect_remote(dir, srv.port());

    const std::string dll = nv_dll_version();
    CHECK(!dll.empty());
    CHECK(dll.find('.') != std::string::npos);

    // Same binary on both ends here: remote and local must agree exactly,
    // and both carry the patch (no "1.9a" truncation).
    CHECK(nv_server_version(hRemote) == dll);
    CHECK(nv_server_version(hLocal) == dll);

    REQUIRE(AdsDisconnect(hRemote) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hLocal) == AE_SUCCESS);
    srv.stop();
}
