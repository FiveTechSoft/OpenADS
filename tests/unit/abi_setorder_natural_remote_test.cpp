// SetOrder iid 0 / missing must not be AE_INTERNAL_ERROR 5000.
// B_BIG dbSetOrder(0) after a racing OpenIndex logged 30x 5000
// ("SetOrder: bad index id") because the session map had no iid.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"
#include "network/client.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE remote_connect(openads::network::Server& srv, const fs::path& dir) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> u(uri.begin(), uri.end());
    u.push_back(0);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) == 0);
    return h;
}

void make_plain(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 def[] = "NAME,C,10,0";
    UNSIGNED8 tname[] = "plain.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

}  // namespace

TEST_CASE("remote SetOrderByHandle(0) with no index is natural, not 5000") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_setord_nat";
    make_plain(dir);
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(srv, dir);
    UNSIGNED8 tname[] = "plain.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, ADS_SHARED, &hT) == 0);

    const UNSIGNED32 rc = AdsSetIndexOrderByHandle(hT, 0);
    CHECK(rc == 0);
    CHECK(rc != openads::AE_INTERNAL_ERROR);

    UNSIGNED8 f[] = "NAME";
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)"ok", 2) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsUnlockRecord(hT, 0) == 0);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("remote SetOrder after CloseAllIndexes is not 5000") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_setord_stale";
    make_plain(dir);
    {
        const auto sp = dir.string();
        std::vector<UNSIGNED8> s(sp.begin(), sp.end());
        s.push_back(0);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(s.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 tname[] = "plain.dbf";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                             0, 0, 0, 0, &hT) == 0);
        UNSIGNED8 bag[] = "plain.cdx";
        UNSIGNED8 tag[] = "IDX01";
        UNSIGNED8 expr[] = "NAME";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr,
                                 ADS_COMPOUND, 0, &hI) == 0);
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(srv, dir);
    UNSIGNED8 tname[] = "plain.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, ADS_SHARED, &hT) == 0);
    ADSHANDLE ah[8] = {0};
    UNSIGNED16 nidx = 8;
    UNSIGNED8 bag[] = "plain.cdx";
    REQUIRE(AdsOpenIndex(hT, bag, ah, &nidx) == 0);
    REQUIRE(nidx >= 1);
    const ADSHANDLE stale = ah[0];
    REQUIRE(AdsCloseAllIndexes(hT) == 0);

    // Natural order after the bag was dropped must Ack, not 5000.
    CHECK(AdsSetIndexOrderByHandle(hT, 0) == 0);

    // A stale tag handle must not surface as AE_INTERNAL_ERROR 5000.
    const UNSIGNED32 rc = AdsSetIndexOrderByHandle(hT, stale);
    CHECK(rc != openads::AE_INTERNAL_ERROR);
    CHECK((rc == 0 || rc == openads::AE_NO_FILE_FOUND ||
           rc == openads::AE_NO_MATCHING_FILE ||
           rc == openads::AE_TABLE_NOT_FOUND));

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("wire SetOrder iid 0 or unknown is not 5000") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_setord_wire";
    make_plain(dir);
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto ot = rc.open_table("plain.dbf");
    REQUIRE(ot.has_value());
    const std::uint32_t tid = ot.value().id;

    auto z = rc.set_order(tid, 0);
    REQUIRE(z.has_value());

    auto miss = rc.set_order(tid, 0x00FFFFFFu);
    REQUIRE_FALSE(miss.has_value());
    CHECK(miss.error().code != static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR));
    CHECK(miss.error().code != 5000);

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
