// tests/unit/network_pool_mt_test.cpp
// Session pool (WAN thread-lanes): one logical AdsConnect60 opens N TCP
// sessions (OPENADS_POOL_SIZE, default 4); each calling thread is pinned
// to a lane so MT apps proceed in parallel while each table stays on the
// session that opened it. Single-threaded callers always land on lane 0.
//
// This test proves the MT contract with an in-process server:
// 4 threads x 2 distinct tables each, open + goto-top + read + close,
// with per-table sentinel values — any cursor cross-talk between lanes
// fails the value checks. A Connect-frame delta proves the pool opened
// multiple sessions behind the single logical handle.
#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "openads/ace.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::uint64_t pool_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op]
        .count.load(std::memory_order_relaxed);
}

void pool_set_env(const char* v) {
#ifdef _WIN32
    std::string kv = std::string("OPENADS_POOL_SIZE=") + v;
    _putenv(kv.c_str());
#else
    setenv("OPENADS_POOL_SIZE", v, 1);
#endif
}

} // namespace

TEST_CASE("Session pool: MT threads open/read/close on parallel lanes") {
    // Deterministic pool size regardless of the field environment.
    const char* prev = std::getenv("OPENADS_POOL_SIZE");
    const bool had_prev = (prev != nullptr);
    const std::string prev_s = had_prev ? prev : std::string();
    pool_set_env("4");

    auto dir = fs::temp_directory_path() / "openads_poolmt";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Fixture: 8 tables, T<i>.DBF, ID values i*100+j (j = 1..3), index BYID.
    constexpr int kTables = 8;
    {
        UNSIGNED8 srv[512]{};
        std::memcpy(srv, dir.string().c_str(), dir.string().size());
        ADSHANDLE hC0 = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
                == AE_SUCCESS);
        UNSIGNED8 def[] = "ID,N,8,0";
        UNSIGNED8 f1[] = "ID";
        for (int t = 0; t < kTables; ++t) {
            char tn[32]{}, bag[32]{};
            std::snprintf(tn, sizeof(tn), "P%d.DBF", t);
            std::snprintf(bag, sizeof(bag), "P%d.CDX", t);
            UNSIGNED8 tnu[32]{}, bagu[64]{}, tagu[] = "BYID", expu[] = "ID";
            std::memcpy(tnu, tn, std::strlen(tn) + 1);
            std::memcpy(bagu, bag, std::strlen(bag) + 1);
            ADSHANDLE hT = 0;
            REQUIRE(AdsCreateTable(hC0, tnu, nullptr, ADS_CDX, 0, 0, 0, 0,
                                   def, &hT) == AE_SUCCESS);
            for (int j = 1; j <= 3; ++j) {
                REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
                REQUIRE(AdsSetDouble(hT, f1, t * 100 + j) == AE_SUCCESS);
                REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
            }
            ADSHANDLE hI = 0;
            REQUIRE(AdsCreateIndex61(hT, bagu, tagu, expu, nullptr, nullptr,
                                     ADS_COMPOUND, 512, &hI) == AE_SUCCESS);
            REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
        }
        REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);
    }

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());

    const std::uint64_t connects_before = pool_op(0x10);

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);

    // One logical connect opened a full pool of sessions behind it.
    CHECK(pool_op(0x10) - connects_before == 4u);

    // 4 threads x 2 tables: open + top + count + first-row value + close.
    // Sentinel values prove no cursor cross-talk between lanes.
    std::atomic<int> failures{0};
    auto worker = [&](int w) {
        for (int k = 0; k < 2; ++k) {
            const int t = w * 2 + k;
            char tn[32]{};
            std::snprintf(tn, sizeof(tn), "P%d.DBF", t);
            UNSIGNED8 tnu[32]{};
            std::memcpy(tnu, tn, std::strlen(tn) + 1);
            ADSHANDLE hT = 0;
            if (AdsOpenTable(hC, tnu, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                != AE_SUCCESS) {
                failures.fetch_add(1);
                continue;
            }
            UNSIGNED32 nrec = 0;
            if (AdsGetRecordCount(hT, 0, &nrec) != AE_SUCCESS || nrec != 3u)
                failures.fetch_add(1);
            if (AdsGotoTop(hT) != AE_SUCCESS) failures.fetch_add(1);
            double v = 0;
            UNSIGNED8 f1[] = "ID";
            if (AdsGetDouble(hT, f1, &v) != AE_SUCCESS) failures.fetch_add(1);
            // Index BYID ascending: first row is the minimum, t*100+1.
            if (static_cast<int>(v) != t * 100 + 1) failures.fetch_add(1);
            if (AdsCloseTable(hT) != AE_SUCCESS) failures.fetch_add(1);
        }
    };
    // Repeat the whole 4-thread phase: the old data race on the shared
    // remote-table store (unlocked park-path erase) hit ~1 run in 30, so
    // one pass per test run was not enough to catch it. Rounds after the
    // first also exercise park/adopt across threads (fresh thread ids
    // deal onto lanes again).
    constexpr int kRounds = 25;
    for (int round = 0; round < kRounds; ++round) {
        std::vector<std::thread> th;
        for (int w = 0; w < 4; ++w) th.emplace_back(worker, w);
        for (auto& t : th) t.join();
    }
    CHECK(failures.load() == 0);

    // Pool + deferred accounting intact: reopen on the same handle works,
    // disconnect is clean (would hang or fail on accounting bugs).
    {
        UNSIGNED8 tnu[] = "P0.DBF";
        ADSHANDLE hT = 0;
        REQUIRE(AdsOpenTable(hC, tnu, nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
                == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    }
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);

    s.stop();
    fs::remove_all(dir, ec);
    if (had_prev)
        pool_set_env(prev_s.c_str());
#ifdef _WIN32
    else
        _putenv("OPENADS_POOL_SIZE=");
#else
    else
        unsetenv("OPENADS_POOL_SIZE");
#endif
}
