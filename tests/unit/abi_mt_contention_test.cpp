// Multithreaded stress tests for the ACE layer (B_BIG runs 7
// hb_threadStart workers against the same shared table; these are the
// in-process equivalents, one connection per thread so OS byte-lock
// contention is real even on a single machine).
//
//   1. N writer threads x M appends each into one table+bag (local) —
//      no lost records, no lost keys, ordered walk stays consistent.
//   2. Same against a remote openads_serverd (per-thread connections).
//   3. Cross-thread lock semantics: RLock contention and release.
//   4. Concurrent readers + writers: readers must always observe a
//      consistent, nondecreasing (key, recno) walk — never a torn tree.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kWriters = 8;
constexpr int kPerWriter = 50;

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

void create_mt_fixture(ADSHANDLE conn) {
    ADSHANDLE hTbl = 0, hIdx = 0;
    REQUIRE(AdsCreateTable(conn, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                           ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                           ADS_CHECKRIGHTS, 0,
                           (UNSIGNED8*)"NAME,C,19;CITY,C,15;INS,N,4",
                           &hTbl) == 0);
    const char* tags[3][2] = {{"IDX01", "NAME"}, {"IDX02", "CITY"},
                              {"IDX03", "INS"}};
    for (auto& t : tags) {
        REQUIRE(AdsCreateIndex61(hTbl, (UNSIGNED8*)"mt.cdx",
                                 (UNSIGNED8*)t[0], (UNSIGNED8*)t[1],
                                 nullptr, nullptr, 0, 0, &hIdx) == 0);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);
}

// One writer thread: own connection, own table handle, M appends.
// Duplicate-heavy keys (10 names) — the split-boundary stress.
std::atomic<int>   g_fail_stage{-1};
std::atomic<UNSIGNED32> g_fail_rc{0};

bool writer_body(ADSHANDLE conn, int id, int per) {
    static const char* names[10] = {"Alice", "Bob", "Phillip", "Charlie",
                                    "Linda", "Finland", "Diana", "Lucy",
                                    "Jony", "Edward"};
    static const char* cities[10] = {"Madrid", "Barcelona", "Panipat",
                                     "Valencia", "Iris", "Dallas",
                                     "Sevilla", "Sevilla", "Walker",
                                     "Bilbao"};
    ADSHANDLE hTbl = 0, hIdx = 0;
    UNSIGNED32 rc;
    auto fail_at = [&](int stage, UNSIGNED32 r) {
        g_fail_stage = stage;
        g_fail_rc    = r;
        return false;
    };
    if ((rc = AdsOpenTable(conn, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                     ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                     ADS_SHARED, &hTbl)) != 0) {
        return fail_at(1, rc);
    }
    if ((rc = AdsOpenIndex(hTbl, (UNSIGNED8*)"mt.cdx", &hIdx, nullptr)) != 0) {
        return fail_at(2, rc);
    }
    for (int i = 0; i < per; ++i) {
        char ins[16];
        std::snprintf(ins, sizeof(ins), "%d", 1000 + id);
        if ((rc = AdsAppendRecord(hTbl)) != 0) return fail_at(3, rc);
        if ((rc = AdsSetString(hTbl, (UNSIGNED8*)"NAME",
                         (UNSIGNED8*)names[(id + i) % 10],
                         (UNSIGNED16)std::strlen(names[(id + i) % 10]))) != 0)
            return fail_at(4, rc);
        if ((rc = AdsSetString(hTbl, (UNSIGNED8*)"CITY",
                         (UNSIGNED8*)cities[(id + i) % 10],
                         (UNSIGNED16)std::strlen(cities[(id + i) % 10]))) != 0)
            return fail_at(5, rc);
        if ((rc = AdsSetString(hTbl, (UNSIGNED8*)"INS", (UNSIGNED8*)ins,
                         (UNSIGNED16)std::strlen(ins))) != 0)
            return fail_at(6, rc);
        if ((rc = AdsWriteRecord(hTbl)) != 0) return fail_at(7, rc);
    }
    if ((rc = AdsCloseTable(hTbl)) != 0) return fail_at(8, rc);
    return true;
}

template <typename ConnectFn>
void run_writers(ConnectFn connect) {
    std::vector<std::thread> pool;
    std::vector<std::atomic<bool>> done(kWriters);
    std::vector<std::atomic<bool>> ok(kWriters);
    for (int t = 0; t < kWriters; ++t) {
        pool.emplace_back([&, t] {
            ADSHANDLE conn = 0;
            if (connect(&conn) != 0) { ok[t] = false; done[t] = true; return; }
            ok[t] = writer_body(conn, t, kPerWriter);
            AdsDisconnect(conn);
            done[t] = true;
        });
    }
    for (auto& th : pool) th.join();
    for (int t = 0; t < kWriters; ++t) {
        CHECK(done[t].load());
        INFO("writer ", t, " failed at stage ", g_fail_stage.load(),
             " rc=", g_fail_rc.load());
        CHECK(ok[t].load());
    }
}

void verify_mt(ADSHANDLE conn, int expect) {
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsOpenTable(conn, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                         ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                         ADS_SHARED, &hTbl) == 0);
    ADSHANDLE idxs[8] = {0};
    UNSIGNED16 nidx = 8;
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"mt.cdx", idxs, &nidx) == 0);
    REQUIRE(nidx == 3);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTbl, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == static_cast<UNSIGNED32>(expect));

    for (UNSIGNED16 t = 0; t < nidx; ++t) {
        UNSIGNED32 kc = 0;
        REQUIRE(AdsGetKeyCount(idxs[t], 0, &kc) == 0);
        CHECK(kc == static_cast<UNSIGNED32>(expect));
    }

    // Ordered walk on IDX01: nondecreasing (key, recno), exactly `expect`.
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
            INFO("prev=[", prev_key, "]@", prev_rec, " cur=[", k, "]@", rec);
            CHECK((k > prev_key || (k == prev_key && rec > prev_rec)));
        }
        prev_key = k;
        prev_rec = rec;
        ++walked;
        REQUIRE(AdsSkip(hTbl, 1) == 0);
    }
    CHECK(walked == expect);
    REQUIRE(AdsCloseTable(hTbl) == 0);
}

} // namespace

TEST_CASE("MT: 8 writer threads x 50 duplicate-key appends (local)") {
    const auto dir = fs::temp_directory_path() / "openads_mt_local";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_mt_fixture(hConn);

    run_writers([&](ADSHANDLE* c) {
        std::string d = dir.string();
        return AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                            ADS_LOCAL_SERVER, nullptr, nullptr, 0, c);
    });
    verify_mt(hConn, kWriters * kPerWriter);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("MT: 8 writer threads x 50 duplicate-key appends (remote server)") {
    const auto dir = fs::temp_directory_path() / "openads_mt_remote";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ServerGuard srv(dir);
    REQUIRE(srv.ok);

    ADSHANDLE hConn = 0;
    connect_remote(srv.uri, &hConn);
    {
        // BISECT: create the fixture via a LOCAL connection (remote
        // create is covered elsewhere) to isolate the 8-connection
        // concurrent open/append path.
        ADSHANDLE hLoc = 0;
        connect_local(dir, &hLoc);
        create_mt_fixture(hLoc);
        AdsDisconnect(hLoc);
    }

    run_writers([&](ADSHANDLE* c) { return 0 == AdsConnect60(
        reinterpret_cast<UNSIGNED8*>(srv.uri.data()), ADS_REMOTE_SERVER,
        nullptr, nullptr, 0, c) ? 0 : 1; });
    verify_mt(hConn, kWriters * kPerWriter);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("MT: record lock contention across threads honours the byte lock") {
    const auto dir = fs::temp_directory_path() / "openads_mt_locks";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_mt_fixture(hConn);
    run_writers([&](ADSHANDLE* c) {
        std::string d = dir.string();
        return AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                            ADS_LOCAL_SERVER, nullptr, nullptr, 0, c);
    });

    ADSHANDLE hA = 0, hB = 0;
    connect_local(dir, &hA);
    connect_local(dir, &hB);
    ADSHANDLE hTA = 0, hTB = 0;
    REQUIRE(AdsOpenTable(hA, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX, ADS_ANSI,
                         ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS, ADS_SHARED,
                         &hTA) == 0);
    REQUIRE(AdsOpenTable(hB, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX, ADS_ANSI,
                         ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS, ADS_SHARED,
                         &hTB) == 0);

    // Thread A locks record 7; thread B's lock call must NOT complete
    // while A holds it (the ABI retry loop keeps it contending). After
    // A's unlock, B's call returns success.
    REQUIRE(AdsGotoRecord(hTA, 7) == 0);
    REQUIRE(AdsLockRecord(hTA, 0) == 0);

    std::atomic<int> b_done{0};
    std::atomic<UNSIGNED32> b_rc{0xFFFFFFFFu};
    std::thread tb([&] {
        AdsGotoRecord(hTB, 7);
        b_rc = AdsLockRecord(hTB, 0);
        b_done = 1;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(b_done.load() == 0);   // still blocked/retrying: real contention
    REQUIRE(AdsUnlockRecord(hTA, 7) == 0);
    tb.join();
    CHECK(b_done.load() == 1);
    CHECK(b_rc.load() == 0);     // acquired after A released

    REQUIRE(AdsCloseTable(hTA) == 0);
    REQUIRE(AdsCloseTable(hTB) == 0);
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

TEST_CASE("MT: readers always see a consistent walk while writers append") {
    const auto dir = fs::temp_directory_path() / "openads_mt_rw";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hConn = 0;
    connect_local(dir, &hConn);
    create_mt_fixture(hConn);

    std::atomic<bool> stop{false};
    std::atomic<int>  reader_fail{0};

    std::vector<std::thread> pool;
    // 4 writers
    for (int t = 0; t < 4; ++t) {
        pool.emplace_back([&, t] {
            ADSHANDLE c = 0;
            std::string d = dir.string();
            if (AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr, 0, &c) != 0)
                return;
            writer_body(c, t, kPerWriter);
            AdsDisconnect(c);
        });
    }
    // 2 readers looping ordered walks until told to stop; every walk
    // must be nondecreasing — a torn read would break that.
    for (int t = 0; t < 2; ++t) {
        pool.emplace_back([&, t] {
            ADSHANDLE c = 0;
            std::string d = dir.string();
            if (AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr, 0, &c) != 0)
                return;
            while (!stop.load()) {
                ADSHANDLE hTbl = 0, hIdx = 0;
                if (AdsOpenTable(c, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                                 ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                                 ADS_CHECKRIGHTS, ADS_SHARED, &hTbl) != 0) {
                    continue;
                }
                if (AdsOpenIndex(hTbl, (UNSIGNED8*)"mt.cdx", &hIdx,
                                 nullptr) != 0) {
                    AdsCloseTable(hTbl);
                    continue;
                }
                AdsGotoTop(hTbl);
                std::string prev_key;
                UNSIGNED32 prev_rec = 0;
                int walked = 0;
                for (;;) {
                    UNSIGNED16 eof = 0;
                    if (AdsAtEOF(hTbl, &eof) != 0 || eof) break;
                    UNSIGNED8 buf[32] = {0};
                    UNSIGNED32 len = sizeof(buf) - 1;
                    if (AdsGetString(hTbl, (UNSIGNED8*)"NAME", buf, &len,
                                     0) != 0) {
                        reader_fail++;
                        break;
                    }
                    std::string k(reinterpret_cast<char*>(buf), len);
                    UNSIGNED32 rec = 0;
                    AdsGetRecordNum(hTbl, 0, &rec);
                    if (walked > 0 &&
                        !(k > prev_key || (k == prev_key && rec > prev_rec))) {
                        reader_fail++;
                    }
                    prev_key = k;
                    prev_rec = rec;
                    ++walked;
                    if (AdsSkip(hTbl, 1) != 0) break;
                }
                AdsCloseTable(hTbl);
            }
            AdsDisconnect(c);
        });
    }

    for (auto& th : pool) {
        if (th.joinable()) {
            if (pool.size() > 4 && &th == &pool[4]) {
                // readers joined after stop below
            }
        }
    }
    // join writers first (first 4 threads), then stop readers
    for (int t = 0; t < 4; ++t) pool[t].join();
    stop = true;
    for (int t = 4; t < 6; ++t) pool[t].join();

    CHECK(reader_fail.load() == 0);
    verify_mt(hConn, 4 * kPerWriter);

    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
