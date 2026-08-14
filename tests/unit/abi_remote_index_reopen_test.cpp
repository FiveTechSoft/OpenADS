// Regression test for Pritpal Bedi's TestIndexes() report (v1.8.76, Harbour
// 32-bit ADSCDX against a remote OpenADS server):
//
//   INDEX ON name TAG "IDX01" TO (cIdx)   // x3 tags
//   ordListClear()                        // -> AdsCloseAllIndexes
//   ordListAdd( cIdx )                    // -> AdsOpenIndex
//   SET ORDER TO 1                        // -> AdsGetIndexHandleByOrder
//   dbGoBottom(); dbSeek( "Alice" )
//
// Run 1 (bag created in the same session) raised ADSCDX/301 "Workarea not
// indexed" at the seek; run 2 (bag already on disk, auto-opened at
// AdsOpenTable) raised "SetOrder [32/5000]". Root cause: the client-side
// remote AdsCloseAllIndexes only sent the wire close and never invalidated
// the cached RemoteTable index state (index_by_tag / index_handles /
// active_index_id). The next AdsOpenIndex dedup kept the stale wire ids
// (5000 on SetOrder) or left index_handles empty (301 on seek).
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

void make_testindex_table(const fs::path& dir, bool with_bag) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // NOTE: build the byte vector from a NAMED string -- constructing it
    // from dir.string().begin()/dir.string().end() mixes iterators of two
    // different temporaries (UB; flaky "vector too long" length_error).
    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 name[] = "TESTINDEX.DBF";
    UNSIGNED8 def[]  = "NAME,C,20,0;CITY,C,20,0;INS,N,4,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    const char* rows[3][3] = {
        {"Alice", "Rome",  "1"},
        {"Bob",   "Milan", "2"},
        {"Carol", "Turin", "3"},
    };
    UNSIGNED8 f_name[] = "NAME";
    UNSIGNED8 f_city[] = "CITY";
    UNSIGNED8 f_ins[]  = "INS";
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, f_name, reinterpret_cast<UNSIGNED8*>(
                         const_cast<char*>(r[0])),
                     static_cast<UNSIGNED32>(std::strlen(r[0])));
        AdsSetString(hT, f_city, reinterpret_cast<UNSIGNED8*>(
                         const_cast<char*>(r[1])),
                     static_cast<UNSIGNED32>(std::strlen(r[1])));
        AdsSetString(hT, f_ins,  reinterpret_cast<UNSIGNED8*>(
                         const_cast<char*>(r[2])),
                     static_cast<UNSIGNED32>(std::strlen(r[2])));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    if (with_bag) {
        UNSIGNED8 bag[]       = "TESTINDEX.CDX";
        UNSIGNED8 tags[3][8]  = {"IDX01", "IDX02", "IDX03"};
        UNSIGNED8 exprs[3][8] = {"NAME",  "CITY",  "INS"};
        for (int i = 0; i < 3; ++i) {
            ADSHANDLE hIdx = 0;
            REQUIRE(AdsCreateIndex61(hT, bag, tags[i], exprs[i],
                        nullptr, nullptr, ADS_COMPOUND, 512, &hIdx) == 0);
        }
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

ADSHANDLE open_remote(ADSHANDLE hConn) {
    UNSIGNED8 name[]  = "TESTINDEX.DBF";
    UNSIGNED8 alias[] = "TSTG";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, ADS_ANSI,
                         ADS_PROPRIETARY_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &hT) == 0);
    return hT;
}

// Pritpal's sequence from ordListClear() on: reopen the bag, resolve
// order 1, go bottom, seek "Alice" -- all must succeed.
void clear_reopen_and_seek(ADSHANDLE hT) {
    UNSIGNED8 bag[] = "TESTINDEX.CDX";
    REQUIRE(AdsCloseAllIndexes(hT) == 0);

    ADSHANDLE arr[8] = {};
    UNSIGNED16 alen  = 8;
    REQUIRE(AdsOpenIndex(hT, bag, arr, &alen) == 0);
    REQUIRE(alen == 3);

    ADSHANDLE hIdx = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hIdx) == 0);   // SET ORDER TO 1
    REQUIRE(AdsGotoBottom(hT) == 0);
    REQUIRE(AdsGetIndexHandleByOrder(hT, 1, &hIdx) == 0);   // SET ORDER TO 1

    UNSIGNED8 key[] = "Alice";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, key, 5, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    REQUIRE(found != 0);
}

} // namespace

TEST_CASE("remote ordListClear + ordListAdd leaves a usable order (created bag)") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_pritpal_reopen1";
    make_testindex_table(dir, /*with_bag=*/false);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hT    = open_remote(hConn);

    // INDEX ON ... TAG ... TO bag, three tags on the same CDX.
    UNSIGNED8 bag[]   = "TESTINDEX.CDX";
    UNSIGNED8 tags[3][8]  = {"IDX01", "IDX02", "IDX03"};
    UNSIGNED8 exprs[3][8] = {"NAME",  "CITY",  "INS"};
    for (int i = 0; i < 3; ++i) {
        ADSHANDLE hIdx = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tags[i], exprs[i],
                    nullptr, nullptr, ADS_COMPOUND, 512, &hIdx) == 0);
    }

    clear_reopen_and_seek(hT);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

TEST_CASE("remote ordListClear + ordListAdd leaves a usable order (auto-opened bag)") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_pritpal_reopen2";
    // Bag built up-front so the remote open auto-opens it as the
    // production index (run-2 scenario: CDX already on disk).
    make_testindex_table(dir, /*with_bag=*/true);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());
    ADSHANDLE hT    = open_remote(hConn);

    clear_reopen_and_seek(hT);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}
