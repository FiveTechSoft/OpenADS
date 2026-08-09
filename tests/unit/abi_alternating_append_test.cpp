// B_BIG coverage gap: alternating independent clients appending
// duplicate keys to a shared DBF+CDX bag.
//
// B_BIG.prg (Pritpal Bedi) stages C:/Temp/TestFolder/TestIndex.dbf with
// 3 tags and then alternates ADS (remote server) and DBFCDX (direct
// file) instances, each appending the SAME 10 names/cities — duplicate
// keys with rising recnos, the exact input that exposed the stale
// parent-separator GPF (hb_cdxPageSeekKey: wrong parent key) at record
// 151.
//
// These cases emulate the alternation in-process:
//   1. two local connections take turns appending bursts;
//   2. an openads_serverd session alternates with a local connection
//      (the real B_BIG topology: remote ADS vs direct file);
// then assert record count, per-tag key counts, first-duplicate seek
// order, and a nondecreasing ordered walk.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kNames[10] = {"Alice", "Bob", "Phillip", "Charlie",
                                    "Linda", "Finland", "Diana", "Lucy",
                                    "Jony", "Edward"};
constexpr const char* kCities[10] = {"Madrid", "Barcelona", "Panipat",
                                     "Valencia", "Iris", "Dallas", "Sevilla",
                                     "Sevilla", "Walker", "Bilbao"};
constexpr int kRounds = 16;   // 16 x 10 = 160 records: past the split point

struct ServerGuard {
    openads::network::Server srv;
    std::string uri;
    bool ok = false;
    explicit ServerGuard(const fs::path& data_dir) {
        if (auto v = srv.start("127.0.0.1", 0)) {
            std::string path = data_dir.string();
            std::replace(path.begin(), path.end(), '\\', '/');
            uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;
            ok = true;
        }
    }
    ~ServerGuard() { srv.stop(); }
};

void connect_local(const fs::path& dir, ADSHANDLE* conn) {
    std::string d = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr, 0, conn) == 0);
}

void connect_remote(const std::string& uri, ADSHANDLE* conn) {
    std::string u = uri;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(u.data()),
                         ADS_REMOTE_SERVER, nullptr, nullptr, 0, conn) == 0);
}

void open_bag(ADSHANDLE conn, const char* leaf, ADSHANDLE* tbl) {
    auto rc1 = AdsOpenTable(conn, (UNSIGNED8*)leaf, nullptr, ADS_CDX, ADS_ANSI,
                            ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                            ADS_SHARED, tbl);
    INFO("open_bag AdsOpenTable rc=", rc1);
    REQUIRE(rc1 == 0);
    // NULL handle array, B_BIG style: ALL bag tags must stay maintained.
    ADSHANDLE hIdx = 0;
    auto rc2 = AdsOpenIndex(*tbl, (UNSIGNED8*)"big.cdx", &hIdx, nullptr);
    INFO("open_bag AdsOpenIndex rc=", rc2);
    REQUIRE(rc2 == 0);
}

void append_burst(ADSHANDLE conn, int round) {
    ADSHANDLE hTbl = 0;
    open_bag(conn, "big.dbf", &hTbl);
    for (int i = 0; i < 10; ++i) {
        char ins[16];
        std::snprintf(ins, sizeof(ins), "%d", 1000 + round);
        REQUIRE(AdsAppendRecord(hTbl) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)kNames[i],
                             (UNSIGNED16)std::strlen(kNames[i])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY", (UNSIGNED8*)kCities[i],
                             (UNSIGNED16)std::strlen(kCities[i])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"INS", (UNSIGNED8*)ins,
                             (UNSIGNED16)std::strlen(ins)) == 0);
        REQUIRE(AdsWriteRecord(hTbl) == 0);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);
}

void create_fixture(ADSHANDLE conn, const char* leaf) {
    ADSHANDLE hTbl = 0, hIdx = 0;
    REQUIRE(AdsCreateTable(conn, (UNSIGNED8*)leaf, nullptr, ADS_CDX, ADS_ANSI,
                           ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS, 0,
                           (UNSIGNED8*)"NAME,C,19;CITY,C,15;INS,N,4",
                           &hTbl) == 0);
    const char* tags[3][2] = {{"IDX01", "NAME"}, {"IDX02", "CITY"},
                              {"IDX03", "INS"}};
    for (auto& t : tags) {
        REQUIRE(AdsCreateIndex61(hTbl, (UNSIGNED8*)"big.cdx",
                                 (UNSIGNED8*)t[0], (UNSIGNED8*)t[1],
                                 nullptr, nullptr, 0, 0, &hIdx) == 0);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);
}

void verify_all(ADSHANDLE conn) {
    ADSHANDLE hTbl = 0;
    open_bag(conn, "big.dbf", &hTbl);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTbl, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == static_cast<UNSIGNED32>(kRounds * 20));

    // Per-tag key counts (all 3 tags must be fully maintained).
    ADSHANDLE idxs[8] = {0};
    UNSIGNED16 nidx = 8;
    // Reopen with an explicit array to grab per-tag handles.
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"big.cdx", idxs, &nidx) == 0);
    REQUIRE(nidx == 3);
    for (UNSIGNED16 t = 0; t < nidx; ++t) {
        UNSIGNED32 kc = 0;
        REQUIRE(AdsGetKeyCount(idxs[t], 0, &kc) == 0);
        CHECK(kc == static_cast<UNSIGNED32>(kRounds * 20));
    }

    // First "Edward" must be the earliest-inserted one (recno 10).
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(idxs[0], (UNSIGNED8*)"Edward", 6, ADS_STRINGKEY,
                    ADS_HARDSEEK, &found) == 0);
    REQUIRE(found == 1);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTbl, 0, &rn) == 0);
    CHECK(rn == 10);

    // Ordered walk on IDX01: (key, recno) must be nondecreasing.
    REQUIRE(AdsSetIndexOrderByHandle(hTbl, idxs[0]) == 0);
    REQUIRE(AdsGotoTop(hTbl) == 0);
    std::string prev_key;
    UNSIGNED32 prev_rec = 0;
    int walked = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
        UNSIGNED8 buf[32] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hTbl, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
        std::string k(reinterpret_cast<char*>(buf), len);
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &rec) == 0);
        if (walked > 0) {
            INFO("order violation: prev=[", prev_key, "] rec=", prev_rec,
                 " cur=[", k, "] rec=", rec, " at walk#", walked);
            CHECK((k > prev_key || (k == prev_key && rec > prev_rec)));
        }
        prev_key = k;
        prev_rec = rec;
        ++walked;
        REQUIRE(AdsSkip(hTbl, 1) == 0);
    }
    CHECK(walked == kRounds * 20);

    REQUIRE(AdsCloseTable(hTbl) == 0);
}

} // namespace

TEST_CASE("Alternating local connections append duplicate keys into a shared bag") {
    const auto dir = fs::temp_directory_path() / "openads_alt_append_local";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hA = 0, hB = 0;
    connect_local(dir, &hA);
    create_fixture(hA, "big.dbf");
    connect_local(dir, &hB);

    for (int round = 0; round < kRounds; ++round) {
        append_burst(hA, round);
        append_burst(hB, round);
    }
    verify_all(hA);

    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Remote server and local client alternate duplicate-key appends") {
    const auto dir = fs::temp_directory_path() / "openads_alt_append_remote";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ServerGuard srv(dir);
    REQUIRE(srv.ok);

    ADSHANDLE hLocal = 0, hRemote = 0;
    connect_local(dir, &hLocal);
    create_fixture(hLocal, "big.dbf");
    connect_remote(srv.uri, &hRemote);

    for (int round = 0; round < kRounds; ++round) {
        append_burst(hRemote, round);   // ADS side (server)
        append_burst(hLocal, round);    // "DBFCDX" side (direct file)
    }
    verify_all(hRemote);
    verify_all(hLocal);

    REQUIRE(AdsDisconnect(hRemote) == 0);
    REQUIRE(AdsDisconnect(hLocal) == 0);
    fs::remove_all(dir, ec);
}

namespace {

void make_populated(const fs::path& dir, int bursts_per_side) {
    ADSHANDLE hA = 0, hB = 0;
    connect_local(dir, &hA);
    create_fixture(hA, "big.dbf");
    connect_local(dir, &hB);
    for (int round = 0; round < bursts_per_side; ++round) {
        append_burst(hA, round);
        append_burst(hB, round);
    }
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
}

} // namespace

TEST_CASE("Natural-order appends with an open bag still maintain every tag") {
    // B_BIG appends with dbSetOrder(0): no controlling order, but the
    // open bag's tags must receive every key.
    const auto dir = fs::temp_directory_path() / "openads_natural_append";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_fixture(hConn, "big.dbf");

    ADSHANDLE hTbl = 0;
    open_bag(hConn, "big.dbf", &hTbl);
    REQUIRE(AdsSetIndexOrder(hTbl, (UNSIGNED8*)"") == 0);   // natural order
    for (int i = 0; i < 10; ++i) {
        REQUIRE(AdsAppendRecord(hTbl) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)kNames[i],
                             (UNSIGNED16)std::strlen(kNames[i])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY", (UNSIGNED8*)kCities[i],
                             (UNSIGNED16)std::strlen(kCities[i])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"INS", (UNSIGNED8*)"1000",
                             4) == 0);
        REQUIRE(AdsWriteRecord(hTbl) == 0);
    }

    ADSHANDLE idxs[8] = {0};
    UNSIGNED16 nidx = 8;
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"big.cdx", idxs, &nidx) == 0);
    REQUIRE(nidx == 3);
    for (UNSIGNED16 t = 0; t < nidx; ++t) {
        UNSIGNED32 kc = 0;
        REQUIRE(AdsGetKeyCount(idxs[t], 0, &kc) == 0);
        CHECK(kc == 10);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Append/commit/unlock churn leaves no locks held") {
    const auto dir = fs::temp_directory_path() / "openads_lock_churn";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_fixture(hConn, "big.dbf");

    ADSHANDLE hTbl = 0;
    open_bag(hConn, "big.dbf", &hTbl);
    for (int i = 0; i < 200; ++i) {
        REQUIRE(AdsAppendRecord(hTbl) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME",
                             (UNSIGNED8*)kNames[i % 10], 4) == 0);
        REQUIRE(AdsWriteRecord(hTbl) == 0);
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &rec) == 0);
        REQUIRE(AdsUnlockRecord(hTbl, rec) == 0);
        UNSIGNED16 nl = 99;
        REQUIRE(AdsGetNumLocks(hTbl, &nl) == 0);
        CHECK(nl == 0);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Local record lock blocks a remote write and vice versa") {
    const auto dir = fs::temp_directory_path() / "openads_cross_contention";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ServerGuard srv(dir);
    REQUIRE(srv.ok);

    ADSHANDLE hLocal = 0;
    connect_local(dir, &hLocal);
    create_fixture(hLocal, "big.dbf");
    make_populated(dir, 1);   // 20 records

    ADSHANDLE hRemote = 0;
    connect_remote(srv.uri, &hRemote);

    ADSHANDLE hLT = 0, hRT = 0;
    open_bag(hLocal, "big.dbf", &hLT);
    open_bag(hRemote, "big.dbf", &hRT);

    const char* val = "Contended";

    // Local RLock on rec 1 -> remote write must hit the lock guard.
    REQUIRE(AdsGotoRecord(hLT, 1) == 0);
    REQUIRE(AdsLockRecord(hLT, 0) == 0);
    REQUIRE(AdsGotoRecord(hRT, 1) == 0);
    UNSIGNED32 rc = AdsLockRecord(hRT, 0);
    CHECK(rc != 0);   // remote lock contends with the local byte lock
    rc = AdsSetString(hRT, (UNSIGNED8*)"NAME", (UNSIGNED8*)val, 9);
    CHECK(rc == 5035);
    REQUIRE(AdsUnlockRecord(hLT, 1) == 0);

    // Remote RLock on rec 2 -> local write must fail the same way.
    REQUIRE(AdsGotoRecord(hRT, 2) == 0);
    REQUIRE(AdsLockRecord(hRT, 0) == 0);
    REQUIRE(AdsGotoRecord(hLT, 2) == 0);
    rc = AdsLockRecord(hLT, 0);
    CHECK(rc != 0);
    rc = AdsSetString(hLT, (UNSIGNED8*)"NAME", (UNSIGNED8*)val, 9);
    CHECK(rc == 5035);
    REQUIRE(AdsUnlockRecord(hRT, 2) == 0);

    REQUIRE(AdsCloseTable(hLT) == 0);
    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRemote) == 0);
    REQUIRE(AdsDisconnect(hLocal) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("Editing an indexed field moves the key and keeps order with duplicates") {
    const auto dir = fs::temp_directory_path() / "openads_key_edit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_fixture(hConn, "big.dbf");
    make_populated(dir, 2);   // 40 records, 4 of each name

    ADSHANDLE hTbl = 0;
    open_bag(hConn, "big.dbf", &hTbl);

    // Recolor record 10 (first "Edward") to "Zzztop".
    REQUIRE(AdsGotoRecord(hTbl, 10) == 0);
    REQUIRE(AdsLockRecord(hTbl, 0) == 0);
    REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME", (UNSIGNED8*)"Zzztop",
                         6) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);
    REQUIRE(AdsUnlockRecord(hTbl, 10) == 0);

    ADSHANDLE idxs[8] = {0};
    UNSIGNED16 nidx = 8;
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"big.cdx", idxs, &nidx) == 0);

    // "Edward" now has 3 keys; "Zzztop" exactly 1, at the end of the order.
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(idxs[0], (UNSIGNED8*)"Edward", 6, ADS_STRINGKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    int edwards = 0;
    for (;;) {
        UNSIGNED8 buf[32] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hTbl, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
        if (std::string(reinterpret_cast<char*>(buf), 6) != "Edward") break;
        ++edwards;
        REQUIRE(AdsSkip(hTbl, 1) == 0);
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
    }
    CHECK(edwards == 3);

    REQUIRE(AdsSeek(idxs[0], (UNSIGNED8*)"Zzztop", 6, ADS_STRINGKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hTbl, 0, &rn) == 0);
    CHECK(rn == 10);

    // Full ordered walk must stay nondecreasing after the key move.
    REQUIRE(AdsSetIndexOrderByHandle(hTbl, idxs[0]) == 0);
    REQUIRE(AdsGotoTop(hTbl) == 0);
    std::string prev;
    int walked = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
        UNSIGNED8 buf[32] = {0};
        UNSIGNED32 len = sizeof(buf) - 1;
        REQUIRE(AdsGetString(hTbl, (UNSIGNED8*)"NAME", buf, &len, 0) == 0);
        std::string k(reinterpret_cast<char*>(buf), len);
        if (walked > 0) CHECK(k >= prev);
        prev = k;
        ++walked;
        REQUIRE(AdsSkip(hTbl, 1) == 0);
    }
    CHECK(walked == 40);

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
