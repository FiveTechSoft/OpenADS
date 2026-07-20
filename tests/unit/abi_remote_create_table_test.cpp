// Remote AdsCreateTable must land the free table under the server data
// directory (not next to the client app) and leave the open handle usable.
// Regression for Pritpal Bedi: v1.8.15 fixed local absolute-path create,
// but remote DbCreate still wrote MyTable.dbf beside the app; the post-
// create AdsOpenTable (remote) then failed with ADSCDX/5103.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(hConn != 0);
    return hConn;
}

} // namespace

TEST_CASE("remote AdsCreateTable lands under server data dir and opens") {
    using openads::network::Server;

    auto data = fs::temp_directory_path() / "openads_remote_create_data";
    auto app  = fs::temp_directory_path() / "openads_remote_create_app";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::remove_all(app, ec);
    fs::create_directories(data);
    fs::create_directories(app);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    // Mimic a client whose CWD is *not* the server data dir.
    const auto prev = fs::current_path();
    fs::current_path(app);
    ADSHANDLE hConn = remote_connect(data, port);

    UNSIGNED8 name[]   = "MyTable.dbf";
    UNSIGNED8 fields[] =
        "Name,Character,30,0;Age,Numeric,3,0;Married,Logical;DOB,Date";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields, &hTable) == 0);
    REQUIRE(hTable != 0);

    // Usable open handle after create (DbCreate post-open).
    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == 0);
    CHECK(nflds == 4);

    UNSIGNED32 nrec = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &nrec) == 0);
    CHECK(nrec == 0u);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fName[] = "Name";
    UNSIGNED8 val[]   = "Pritpal";
    REQUIRE(AdsSetString(hTable, fName, val, 7) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &nrec) == 0);
    CHECK(nrec == 1u);

    REQUIRE(AdsCloseTable(hTable) == 0);

    // File is on the server data dir, NOT next to the client app.
    CHECK(fs::exists(data / "MyTable.dbf"));
    CHECK_FALSE(fs::exists(app / "MyTable.dbf"));

    // Re-open by bare name over the same remote connection.
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, ADS_ANSI, 0, 0, 0,
                         &hTable) == 0);
    nflds = 0;
    REQUIRE(AdsGetNumFields(hTable, &nflds) == 0);
    CHECK(nflds == 4);
    REQUIRE(AdsCloseTable(hTable) == 0);

    // Drop over the wire removes the server-side file.
    REQUIRE(AdsDropTable(hConn, name, 1) == 0);
    CHECK_FALSE(fs::exists(data / "MyTable.dbf"));

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::current_path(prev);
    fs::remove_all(data, ec);
    fs::remove_all(app, ec);
}

TEST_CASE("remote AdsCreateTable with drive-rooted name still under data dir") {
    using openads::network::Server;

    auto data = fs::temp_directory_path() / "openads_remote_create_abs";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(data, srv.port());

    // Client passes an absolute path rooted at its own drive — the
    // server must fold it under the data directory (1.8.15 local fix
    // applied on the server-side AdsCreateTable path).
    const std::string root = fs::path(data).root_path().string();
    const std::string leaf = "remote_abs_stray";
    const std::string abs_name = (fs::path(root) / (leaf + ".dbf")).string();

    UNSIGNED8 name[512];
    std::memcpy(name, abs_name.c_str(), abs_name.size() + 1);
    UNSIGNED8 fields[] = "ID,Numeric,4,0;NAME,Character,8";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX, 0, 0, 0, 0,
                           fields, &hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);

    CHECK(fs::exists(data / (leaf + ".dbf")));
    CHECK_FALSE(fs::exists(fs::path(abs_name)));

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(data, ec);
}
