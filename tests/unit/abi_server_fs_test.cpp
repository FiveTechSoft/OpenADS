// Local + remote Ads* filesystem API (oads_* backend).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <chrono>
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
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    REQUIRE(hConn != 0);
    return hConn;
}

[[maybe_unused]] ADSHANDLE local_connect(const fs::path& dir) {
    // Named string first: iterators of two dir.string() temporaries must
    // never be mixed in one vector range (UB, flaky "vector too long").
    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> buf(dir_str.begin(), dir_str.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);
    REQUIRE(hConn != 0);
    return hConn;
}

} // namespace

TEST_CASE("local Ads* filesystem: create write size erase") {
    std::error_code ec;
    auto base = fs::temp_directory_path(ec);
    REQUIRE_FALSE(ec);
    auto data = base / "oads_fs_loc";
    fs::remove_all(data, ec);
    fs::create_directories(data, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(fs::is_directory(data, ec));

    // Short path, no REQUIRE inside connect (surface return codes).
    std::string path = data.string();
    std::vector<UNSIGNED8> cbuf(path.begin(), path.end());
    cbuf.push_back(0);
    ADSHANDLE h = 0;
    UNSIGNED32 crc = AdsConnect60(cbuf.data(), ADS_LOCAL_SERVER, nullptr,
                                  nullptr, 0, &h);
    INFO("connect rc=", crc, " h=", h, " path=", path);
    REQUIRE(crc == 0);
    REQUIRE(h != 0);

    UNSIGNED32 rc = AdsDirMake(h, (UNSIGNED8*)"inbox");
    REQUIRE(rc == 0);
    UNSIGNED16 ex = 0;
    rc = AdsDirExist(h, (UNSIGNED8*)"inbox", &ex);
    REQUIRE(rc == 0);
    CHECK(ex == 1);

    ADSHANDLE hf = 0;
    rc = AdsFCreate(h, (UNSIGNED8*)"inbox/hello.txt", 0, &hf);
    INFO("FCreate rc=", rc, " hf=", hf);
    REQUIRE(rc == 0);
    REQUIRE(hf != 0);
    const char* msg = "hello-oads";
    UNSIGNED32 nw = 0;
    rc = AdsFWrite(hf, msg, 10, &nw);
    INFO("FWrite rc=", rc, " nw=", nw);
    REQUIRE(rc == 0);
    CHECK(nw == 10);
    REQUIRE(AdsFClose(hf) == 0);

    UNSIGNED32 sz = 0;
    rc = AdsGetFileSize(h, (UNSIGNED8*)"inbox/hello.txt", &sz);
    INFO("GetFileSize rc=", rc, " sz=", sz);
    REQUIRE(rc == 0);
    CHECK(sz == 10);

    ex = 0;
    REQUIRE(AdsCheckExistence(h, (UNSIGNED8*)"inbox/hello.txt", &ex) == 0);
    CHECK(ex == 1);

    REQUIRE(AdsRenameFile(h, (UNSIGNED8*)"inbox/hello.txt",
                          (UNSIGNED8*)"inbox/bye.txt") == 0);
    REQUIRE(AdsDeleteFile(h, (UNSIGNED8*)"inbox/bye.txt") == 0);
    REQUIRE(AdsDirRemove(h, (UNSIGNED8*)"inbox") == 0);
    REQUIRE(AdsDisconnect(h) == 0);
    fs::remove_all(data, ec);
}

TEST_CASE("remote Ads* filesystem: EnableFileFunc off denies") {
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "oads_abi_fs_deny";
    fs::remove_all(data);
    fs::create_directories(data);

    Server srv;
    srv.set_data_dir(data.string());
    srv.set_enable_file_func(false);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    ADSHANDLE h = remote_connect(data, srv.port());
    UNSIGNED16 ex = 1;
    CHECK(AdsCheckExistence(h, (UNSIGNED8*)"a.txt", &ex) ==
          openads::AE_ACCESS_DENIED);
    REQUIRE(AdsDisconnect(h) == 0);
    fs::remove_all(data);
}

TEST_CASE("remote Ads* filesystem: full round-trip under data dir") {
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "oads_abi_fs_remote";
    auto app  = fs::temp_directory_path() / "oads_abi_fs_remote_app";
    fs::remove_all(data);
    fs::remove_all(app);
    fs::create_directories(data);
    fs::create_directories(app);

    Server srv;
    srv.set_data_dir(data.string());
    srv.set_enable_file_func(true);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto prev = fs::current_path();
    fs::current_path(app);

    ADSHANDLE h = remote_connect(data, srv.port());
    REQUIRE(AdsDirMake(h, (UNSIGNED8*)"box") == 0);

    ADSHANDLE hf = 0;
    REQUIRE(AdsFCreate(h, (UNSIGNED8*)"box/note.txt", 0, &hf) == 0);
    const char* body = "remote-fs";
    UNSIGNED32 nw = 0;
    REQUIRE(AdsFWrite(hf, body, 9, &nw) == 0);
    CHECK(nw == 9);
    REQUIRE(AdsFClose(hf) == 0);

    UNSIGNED32 sz = 0;
    REQUIRE(AdsGetFileSize(h, (UNSIGNED8*)"box/note.txt", &sz) == 0);
    CHECK(sz == 9);

    // Must NOT appear next to client app.
    CHECK_FALSE(fs::exists(app / "box" / "note.txt"));
    CHECK(fs::exists(data / "box" / "note.txt"));

    UNSIGNED8 tbuf[16]{};
    UNSIGNED16 tlen = 16;
    REQUIRE(AdsGetFileTime(h, (UNSIGNED8*)"box/note.txt", tbuf, &tlen) == 0);
    CHECK(tlen >= 8);

    UNSIGNED8 dbuf[16]{};
    UNSIGNED16 dlen = 16;
    REQUIRE(AdsGetFileDate(h, (UNSIGNED8*)"box/note.txt", dbuf, &dlen) == 0);

    // Directory listing (size probe then fill).
    UNSIGNED32 need = 0;
    UNSIGNED32 drc = AdsDirectory(h, (UNSIGNED8*)"box/*.*", 0, nullptr, &need);
    REQUIRE(drc == openads::AE_INSUFFICIENT_BUFFER);
    REQUIRE(need > 0);
    REQUIRE(need < 1024u * 1024u);
    std::vector<UNSIGNED8> pack(need);
    REQUIRE(AdsDirectory(h, (UNSIGNED8*)"box/*.*", 0, pack.data(), &need) ==
            0);
    CHECK(need > 0);

    // Re-open read
    hf = 0;
    REQUIRE(AdsFOpen(h, (UNSIGNED8*)"box/note.txt", 0, &hf) == 0);
    char rbuf[32]{};
    UNSIGNED32 nr = 0;
    REQUIRE(AdsFRead(hf, rbuf, 32, &nr) == 0);
    CHECK(nr == 9);
    CHECK(std::string(rbuf, rbuf + nr) == "remote-fs");
    REQUIRE(AdsFClose(hf) == 0);

    REQUIRE(AdsDeleteFile(h, (UNSIGNED8*)"box/note.txt") == 0);
    REQUIRE(AdsDirRemove(h, (UNSIGNED8*)"box") == 0);
    REQUIRE(AdsDisconnect(h) == 0);

    fs::current_path(prev);
    fs::remove_all(data);
    fs::remove_all(app);
}
