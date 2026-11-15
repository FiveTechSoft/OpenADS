// AdsGetRecordCRC, AdsCustomizeAOF, AdsSetServerType.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
ADSHANDLE open_conn(const fs::path& dir) {
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);
    return hConn;
}

// Count records reachable from GotoTop via Skip(1) until EOF — honours an
// active AOF / filter.
int visible_count(ADSHANDLE hT) {
    if (AdsGotoTop(hT) != openads::AE_SUCCESS) return -1;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(hT, &eof) != openads::AE_SUCCESS) return -1;
        if (eof) break;
        ++n;
        if (AdsSkip(hT, 1) != openads::AE_SUCCESS) return -1;
    }
    return n;
}
}  // namespace

TEST_CASE("AdsGetRecordCRC is stable per record and changes when the record changes") {
    const auto dir = fs::temp_directory_path() / "openads_record_crc";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "ID,Numeric,4,0;NAME,Character,8";
    UNSIGNED8 tname[] = "crc";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == openads::AE_SUCCESS);

    UNSIGNED8 fID[8]   = "ID";
    UNSIGNED8 fNAME[8] = "NAME";

    REQUIRE(AdsAppendRecord(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fID, 1.0) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fNAME, (UNSIGNED8*)"AAAA", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == openads::AE_SUCCESS);

    UNSIGNED32 crc1 = 0;
    REQUIRE(AdsGetRecordCRC(hT, &crc1, 0) == openads::AE_SUCCESS);
    CHECK(crc1 != 0u);

    // Same record, read again -> identical CRC.
    UNSIGNED32 crc1b = 0;
    REQUIRE(AdsGetRecordCRC(hT, &crc1b, 0) == openads::AE_SUCCESS);
    CHECK(crc1b == crc1);

    // Append a different record -> different CRC.
    REQUIRE(AdsAppendRecord(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsSetDouble(hT, fID, 2.0) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hT, fNAME, (UNSIGNED8*)"BBBB", 4) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == openads::AE_SUCCESS);

    UNSIGNED32 crc2 = 0;
    REQUIRE(AdsGetRecordCRC(hT, &crc2, 0) == openads::AE_SUCCESS);
    CHECK(crc2 != crc1);

    // Going back to record 1 reproduces its original CRC.
    REQUIRE(AdsGotoTop(hT) == openads::AE_SUCCESS);
    UNSIGNED32 crc1c = 0;
    REQUIRE(AdsGetRecordCRC(hT, &crc1c, 0) == openads::AE_SUCCESS);
    CHECK(crc1c == crc1);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCustomizeAOF adds and removes individual records from the AOF set") {
    const auto dir = fs::temp_directory_path() / "openads_customize_aof";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "N,Numeric,4,0";
    UNSIGNED8 tname[] = "aof";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == openads::AE_SUCCESS);
    UNSIGNED8 fN[8] = "N";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == openads::AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, fN, (double)i) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == openads::AE_SUCCESS);
    }

    // AOF N <= 3 -> visible {1,2,3}.
    std::string cond = "N <= 3";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)cond.data(), 0) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    // Force record 4 into the set -> {1,2,3,4}.
    UNSIGNED32 add[] = {4};
    REQUIRE(AdsCustomizeAOF(hT, 1, add, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 4);

    // Force record 2 out of the set -> {1,3,4}.
    UNSIGNED32 rem[] = {2};
    REQUIRE(AdsCustomizeAOF(hT, 1, rem, ADS_AOF_REMOVE_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    // Invalid option is rejected; an out-of-range option does not corrupt the set.
    UNSIGNED32 any[] = {1};
    CHECK(AdsCustomizeAOF(hT, 1, any, 99) != openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCustomizeAOF creates the AOF when none is active; .T./.F. seed AOFs") {
    const auto dir = fs::temp_directory_path() / "openads_customize_aof_noaof";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "N,Numeric,4,0";
    UNSIGNED8 tname[] = "aof";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == openads::AE_SUCCESS);
    UNSIGNED8 fN[8] = "N";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hT) == openads::AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT, fN, (double)i) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == openads::AE_SUCCESS);
    }

    // No AOF + ADD -> AOF holds exactly the listed records.
    UNSIGNED32 add[] = {2, 4};
    REQUIRE(AdsCustomizeAOF(hT, 2, add, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 2);
    REQUIRE(AdsClearAOF(hT) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 5);

    // No AOF + REMOVE -> everything except the listed records.
    UNSIGNED32 rem[] = {1};
    REQUIRE(AdsCustomizeAOF(hT, 1, rem, ADS_AOF_REMOVE_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 4);
    REQUIRE(AdsClearAOF(hT) == openads::AE_SUCCESS);

    // ".F." seeds an empty AOF; customize fills it.
    std::string f = ".F.";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)f.data(), 0) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 0);
    UNSIGNED32 add3[] = {1, 3, 5};
    REQUIRE(AdsCustomizeAOF(hT, 3, add3, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);
    // Out-of-range recnos are ignored, not an error.
    UNSIGNED32 bad[] = {0, 99};
    REQUIRE(AdsCustomizeAOF(hT, 2, bad, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    // " ( .t. ) " seeds a full AOF.
    std::string t = " ( .t. ) ";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)t.data(), 0) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 5);
    REQUIRE(AdsRefreshAOF(hT) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 5);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCustomizeAOF remote wire: .F. seed and no-AOF add") {
    const auto dir = fs::temp_directory_path() / "openads_customize_aof_wire_seed";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "N,Numeric,4,0";
    UNSIGNED8 tname[] = "aof";
    ADSHANDLE hSeed = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hSeed)
            == openads::AE_SUCCESS);
    UNSIGNED8 fN[8] = "N";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hSeed) == openads::AE_SUCCESS);
        REQUIRE(AdsSetDouble(hSeed, fN, (double)i) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hSeed) == openads::AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hSeed) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::string path = dir.string();
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri.c_str(), uri.size() + 1);
    hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hT)
            == openads::AE_SUCCESS);

    std::string f = ".F.";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)f.data(), 0) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 0);
    UNSIGNED32 add[] = {2, 5};
    REQUIRE(AdsCustomizeAOF(hT, 2, add, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 2);

    REQUIRE(AdsClearAOF(hT) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 5);
    UNSIGNED32 one[] = {3};
    REQUIRE(AdsCustomizeAOF(hT, 1, one, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 1);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsCustomizeAOF remote wire: add/remove records from AOF set") {
    const auto dir = fs::temp_directory_path() / "openads_customize_aof_wire";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "N,Numeric,4,0";
    UNSIGNED8 tname[] = "aof";
    ADSHANDLE hSeed = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hSeed)
            == openads::AE_SUCCESS);
    UNSIGNED8 fN[8] = "N";
    for (int i = 1; i <= 5; ++i) {
        REQUIRE(AdsAppendRecord(hSeed) == openads::AE_SUCCESS);
        REQUIRE(AdsSetDouble(hSeed, fN, (double)i) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hSeed) == openads::AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hSeed) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::string path = dir.string();
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri.c_str(), uri.size() + 1);
    hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hT)
            == openads::AE_SUCCESS);

    std::string cond = "N <= 3";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)cond.data(), 0) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    UNSIGNED32 add[] = {4};
    REQUIRE(AdsCustomizeAOF(hT, 1, add, ADS_AOF_ADD_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 4);

    UNSIGNED32 rem[] = {2};
    REQUIRE(AdsCustomizeAOF(hT, 1, rem, ADS_AOF_REMOVE_RECORD) == openads::AE_SUCCESS);
    CHECK(visible_count(hT) == 3);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsGetRecordCRC remote wire matches local CRC") {
    const auto dir = fs::temp_directory_path() / "openads_record_crc_wire";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE hConn = open_conn(dir);

    UNSIGNED8 def[]   = "ID,Numeric,4,0;NAME,Character,8";
    UNSIGNED8 tname[] = "crc";
    ADSHANDLE hLocal = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hLocal)
            == openads::AE_SUCCESS);

    UNSIGNED8 fID[8]   = "ID";
    UNSIGNED8 fNAME[8] = "NAME";
    REQUIRE(AdsAppendRecord(hLocal) == openads::AE_SUCCESS);
    REQUIRE(AdsSetDouble(hLocal, fID, 42.0) == openads::AE_SUCCESS);
    REQUIRE(AdsSetString(hLocal, fNAME, (UNSIGNED8*)"REMOTE", 6) == openads::AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hLocal) == openads::AE_SUCCESS);

    UNSIGNED32 local_crc = 0;
    REQUIRE(AdsGetRecordCRC(hLocal, &local_crc, 0) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hLocal) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    std::string path = dir.string();
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;

    UNSIGNED8 srvbuf[512]{};
    std::memcpy(srvbuf, uri.c_str(), uri.size() + 1);
    hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hT)
            == openads::AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == openads::AE_SUCCESS);

    UNSIGNED32 remote_crc = 0;
    REQUIRE(AdsGetRecordCRC(hT, &remote_crc, 0) == openads::AE_SUCCESS);
    CHECK(remote_crc == local_crc);

    REQUIRE(AdsCloseTable(hT) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetServerType accepts a server-type mask") {
    CHECK(AdsSetServerType(ADS_LOCAL_SERVER) == openads::AE_SUCCESS);
    CHECK(AdsSetServerType(ADS_REMOTE_SERVER) == openads::AE_SUCCESS);
    CHECK(AdsSetServerType(ADS_LOCAL_SERVER | ADS_REMOTE_SERVER)
          == openads::AE_SUCCESS);
}
