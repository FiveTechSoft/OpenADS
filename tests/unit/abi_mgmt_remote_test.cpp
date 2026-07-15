#include "doctest.h"

#include "network/server.h"
#include "openads/ace.h"

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("M9.25 AdsMgGetActivityInfo over the wire sees a live session") {
    using openads::network::Server;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::uint16_t port = srv.port();

    Server::SessionInfo a;
    a.peer_ip = "127.0.0.1";
    a.peer_port = 6001;
    a.user = "alice";
    a.open_tables = 1;
    std::uint64_t id = srv.register_session(a);

    std::string server = "127.0.0.1:" + std::to_string(port);
    std::vector<UNSIGNED8> srvbuf(server.begin(), server.end());
    srvbuf.push_back(0);
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";

    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srvbuf.data(), usr, pwd, &h) == 0);

    ADS_MGMT_ACTIVITY_INFO act;
    UNSIGNED16 sz = sizeof(act);
    REQUIRE(AdsMgGetActivityInfo(h, &act, &sz) == 0);
    // At least the manually-registered "alice" session must show; the
    // mgmt client's own socket may or may not also be counted.
    CHECK(act.stConnections.ulInUse >= 1);

    REQUIRE(AdsMgDisconnect(h) == 0);
    srv.unregister_session(id);
    srv.stop();
}

TEST_CASE("AdsMgGetInstallInfo over the wire reports the SERVER's version") {
    using openads::network::Server;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    std::string server = "127.0.0.1:" + std::to_string(srv.port());
    std::vector<UNSIGNED8> srvbuf(server.begin(), server.end());
    srvbuf.push_back(0);
    UNSIGNED8 usr[2] = "u";
    UNSIGNED8 pwd[2] = "p";
    ADSHANDLE h = 0;
    REQUIRE(AdsMgConnect(srvbuf.data(), usr, pwd, &h) == 0);

    ADS_MGMT_INSTALL_INFO info;
    UNSIGNED16 sz = sizeof(info);
    REQUIRE(AdsMgGetInstallInfo(h, &info, &sz) == 0);
    std::string ver(reinterpret_cast<const char*>(info.aucVersionStr));
    // The remote path answers from the server's HelloAck, which since
    // 1.8.14 carries the real build version. This is the supported way for
    // an app (rddads exposes AdsMgConnect/AdsMgGetInstallInfo) to prove
    // which serverd binary it is actually talking to. Release builds
    // report "OpenADS X.Y.Z"; dev builds may drop the brand to fit the
    // 16-byte field, so assert on what must NOT appear instead.
    CHECK_FALSE(ver.empty());
    CHECK(ver != "OpenADS 1.0");     // the old hardcoded local string
    CHECK(ver != "OpenADS 0.3.2");   // a pre-1.8.14 server's HelloAck
    CHECK(ver.find("0.3.2") == std::string::npos);

    REQUIRE(AdsMgDisconnect(h) == 0);
    srv.stop();
}
