// Remote AdsCreateTable must land the free table under the server data
// directory (not next to the client app) and leave the open handle usable.
// Regression for Pritpal Bedi: v1.8.15 fixed local absolute-path create,
// but remote DbCreate still wrote MyTable.dbf beside the app; the post-
// create AdsOpenTable (remote) then failed with ADSCDX/5103.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
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

TEST_CASE("CreateTable diagnostic isolates post-write reopen failure") {
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "openads_create_diag_post_open";
    auto diag = data / "create-diag.log";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data);
#if defined(_WIN32)
    // Windows MAX_PATH cannot express this long-path reopen fixture. Instead
    // force failure at companion memo creation, still after the DBF write.
    const std::string rel = "probe.dbf";
    fs::create_directories(data / "probe.fpt");
    // Keep the directory non-empty so the server's pre-create remove()
    // cannot erase it; the memo create must then fail on Windows.
    std::ofstream(data / "probe.fpt" / "occupy") << "x";
#else
    // The server's ABI create writes the requested relative path, but its
    // fixed 260-byte post-create name buffer truncates a longer path. This
    // deterministically fails the reopen after the DBF has been written.
    std::string rel;
    for (int i = 0; i < 55; ++i) rel += "nest/";
    rel += "probe.dbf";
    fs::create_directories(data / fs::path(rel).parent_path());
#endif
#if defined(_WIN32)
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILE", diag.string().c_str());
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILTER", "probe.dbf");
#else
    setenv("OPENADS_CREATE_TABLE_DIAG_FILE", diag.string().c_str(), 1);
    setenv("OPENADS_CREATE_TABLE_DIAG_FILTER", "probe.dbf", 1);
#endif
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE conn = remote_connect(data, srv.port());
    std::vector<UNSIGNED8> name(rel.begin(), rel.end());
    name.push_back(0);
#if defined(_WIN32)
    UNSIGNED8 fields[] = "ID,Numeric,4,0;NOTE,Memo";
#else
    UNSIGNED8 fields[] = "ID,Numeric,4,0";
#endif
    ADSHANDLE table = 0;
    const UNSIGNED32 rc = AdsCreateTable(conn, name.data(), nullptr, ADS_CDX, ADS_ANSI,
                                         0, 0, 0, fields, &table);
    const UNSIGNED32 reported = rc;
    UNSIGNED32 last = 0;
    UNSIGNED8 message[512]{};
    UNSIGNED16 message_len = sizeof(message);
    REQUIRE(AdsGetLastError(&last, message, &message_len) == 0);
    CHECK(rc != 0);
    CHECK(last == reported);
    CHECK(fs::exists(data / rel));
    std::ifstream log(diag);
    std::string content((std::istreambuf_iterator<char>(log)),
                        std::istreambuf_iterator<char>());
    CHECK(content.find("stage=dbf-write-ok") != std::string::npos);
#if defined(_WIN32)
    CHECK(content.find("stage=memo-create-fail code=" +
                       std::to_string(reported)) != std::string::npos);
#else
    CHECK(content.find("stage=server-abi-reopen-fail code=" +
                       std::to_string(reported)) != std::string::npos);
#endif
    CHECK(content.find("stage=client-wire-fail code=" +
                       std::to_string(reported)) != std::string::npos);
    CHECK(content.find("NOTE") == std::string::npos);
    (void)AdsDisconnect(conn);
#if defined(_WIN32)
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILE", "");
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILTER", "");
#else
    unsetenv("OPENADS_CREATE_TABLE_DIAG_FILE");
    unsetenv("OPENADS_CREATE_TABLE_DIAG_FILTER");
#endif
    fs::remove_all(data, ec);
}

TEST_CASE("CreateTable broad diagnostic records first-user open and append") {
    using openads::network::Server;
    auto data = fs::temp_directory_path() / "openads_create_diag_first_user";
    auto diag = data / "create-diag.log";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data);
#if defined(_WIN32)
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILE", diag.string().c_str());
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILTER", "");
#else
    setenv("OPENADS_CREATE_TABLE_DIAG_FILE", diag.string().c_str(), 1);
    unsetenv("OPENADS_CREATE_TABLE_DIAG_FILTER");
#endif
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE conn = remote_connect(data, srv.port());
    UNSIGNED8 name[] = "USERCFG.dbf";
    UNSIGNED8 fields[] = "ID,Numeric,4,0";
    ADSHANDLE table = 0;
    REQUIRE(AdsCreateTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields, &table) == 0);
    REQUIRE(AdsAppendRecord(table) == 0);
    REQUIRE(AdsCloseTable(table) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
    std::ifstream log(diag);
    std::string content((std::istreambuf_iterator<char>(log)),
                        std::istreambuf_iterator<char>());
    CHECK(content.find("table=USERCFG.dbf stage=dbf-write-ok") != std::string::npos);
    CHECK(content.find("table=USERCFG.dbf stage=server-open-ok") != std::string::npos);
    CHECK(content.find("table=USERCFG.dbf stage=client-append-enter") != std::string::npos);
    CHECK(content.find("table=USERCFG.dbf stage=client-append-ok") != std::string::npos);
    CHECK(content.find("table=USERCFG.dbf stage=server-first-append-ok") != std::string::npos);
#if defined(_WIN32)
    _putenv_s("OPENADS_CREATE_TABLE_DIAG_FILE", "");
#else
    unsetenv("OPENADS_CREATE_TABLE_DIAG_FILE");
#endif
    fs::remove_all(data, ec);
}
