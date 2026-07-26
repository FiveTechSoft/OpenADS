// AdsGetRecord / AdsSetRecord round-trip. AdsGetRecord returns the raw
// physical DBF record image (deletion-flag byte + field bytes); AdsSetRecord
// writes such an image back over the current record and re-syncs indexes.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("AdsGetRecord / AdsSetRecord round-trip the raw record image") {
    const auto dir = fs::temp_directory_path() / "openads_getset_record";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // ID Numeric(4,0) at record offset 1, NAME Character(8) at offset 5.
    // Physical record length = 1 (del flag) + 4 + 8 = 13.
    UNSIGNED8 name[64]   = "rr";
    UNSIGNED8 alias[64]  = "rr";
    UNSIGNED8 fields[64] = "ID,Numeric,4,0;NAME,Character,8";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                           fields, &hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);

    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 0, &hTable)
            == openads::AE_SUCCESS);

    UNSIGNED8 fID[8]   = "ID";
    UNSIGNED8 fNAME[8] = "NAME";

    REQUIRE(AdsAppendRecord(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fID,   (UNSIGNED8*)"1",    1) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fNAME, (UNSIGNED8*)"AAAA", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == openads::AE_SUCCESS);

    // Size query: a null buffer reports the required length.
    UNSIGNED32 need = 0;
    REQUIRE(AdsGetRecord(hTable, nullptr, &need) == openads::AE_SUCCESS);
    CHECK(need == 13u);

    // Too-small buffer reports AE_INSUFFICIENT_BUFFER and the required size.
    UNSIGNED8 tiny[2];
    UNSIGNED32 tinyLen = sizeof(tiny);
    CHECK(AdsGetRecord(hTable, tiny, &tinyLen) == openads::AE_INSUFFICIENT_BUFFER);
    CHECK(tinyLen == 13u);

    // Full read of the raw image.
    UNSIGNED8 buf[64];
    UNSIGNED32 len = sizeof(buf);
    REQUIRE(AdsGetRecord(hTable, buf, &len) == openads::AE_SUCCESS);
    CHECK(len == 13u);
    CHECK(buf[0] == ' ');                                  // not deleted
    CHECK(std::memcmp(buf + 5, "AAAA    ", 8) == 0);       // NAME, blank-padded

    // Mutate NAME in the raw image and write it straight back.
    // In shared mode, AdsSetRecord requires a lock (GoHot write guard).
    REQUIRE(AdsLockTable(hTable) == openads::AE_SUCCESS);
    std::memcpy(buf + 5, "BBBBBBBB", 8);
    REQUIRE(AdsSetRecord(hTable, buf, len) == openads::AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hTable) == openads::AE_SUCCESS);

    // Field read reflects the raw write.
    UNSIGNED8 out[16];
    UNSIGNED32 outLen = sizeof(out);
    REQUIRE(AdsGetString(hTable, fNAME, out, &outLen, 0) == openads::AE_SUCCESS);
    CHECK(std::string((char*)out, outLen) == "BBBBBBBB");

    // The change survives a close/reopen (it was flushed to disk).
    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 0, &hTable)
            == openads::AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == openads::AE_SUCCESS);
    outLen = sizeof(out);
    REQUIRE(AdsGetString(hTable, fNAME, out, &outLen, 0) == openads::AE_SUCCESS);
    CHECK(std::string((char*)out, outLen) == "BBBBBBBB");

    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsGetRecord / AdsSetRecord round-trip over tcp://") {
    const auto dir = fs::temp_directory_path() / "openads_getset_record_wire";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    UNSIGNED8 name[64]   = "rr";
    UNSIGNED8 alias[64]  = "rr";
    UNSIGNED8 fields[64] = "ID,Numeric,4,0;NAME,Character,8";
    ADSHANDLE hSeed = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                           fields, &hSeed) == openads::AE_SUCCESS);

    UNSIGNED8 fID[8]   = "ID";
    UNSIGNED8 fNAME[8] = "NAME";
    REQUIRE(AdsAppendRecord(hSeed) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hSeed, fID,   (UNSIGNED8*)"1",    1) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hSeed, fNAME, (UNSIGNED8*)"AAAA", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hSeed) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hSeed) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    openads::network::Server tcp_srv;
    REQUIRE(tcp_srv.start("127.0.0.1", 0).has_value());
    std::string path = dir.string();
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "tcp://127.0.0.1:" + std::to_string(tcp_srv.port()) + "/" + path;

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri.c_str(), uri.size() + 1);
    hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == openads::AE_SUCCESS);
    REQUIRE(AdsGotoTop(hTable) == openads::AE_SUCCESS);

    UNSIGNED8 buf[64];
    UNSIGNED32 len = sizeof(buf);
    REQUIRE(AdsGetRecord(hTable, buf, &len) == openads::AE_SUCCESS);
    CHECK(len == 13u);
    CHECK(buf[0] == ' ');
    CHECK(std::memcmp(buf + 5, "AAAA    ", 8) == 0);

    REQUIRE(AdsLockRecord(hTable, 1) == openads::AE_SUCCESS);
    std::memcpy(buf + 5, "WIREWIRE", 8);
    REQUIRE(AdsSetRecord(hTable, buf, len) == openads::AE_SUCCESS);

    UNSIGNED8 out[16];
    UNSIGNED32 outLen = sizeof(out);
    REQUIRE(AdsGetString(hTable, fNAME, out, &outLen, 0) == openads::AE_SUCCESS);
    CHECK(std::string((char*)out, outLen) == "WIREWIRE");

    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    tcp_srv.stop();
    fs::remove_all(dir, ec);
}
