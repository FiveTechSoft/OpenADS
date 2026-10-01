// Append must not wait forever on a record lock. With a FLock covering
// every rec-lock byte, AdsAppendRecord used to block in LockFileEx.
// The append queue + lock budget must return AE_LOCKED (5012) and leave
// no durable blank row.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("append against FLock times out with 5012 and no blank row" *
          doctest::timeout(4)) {
    auto dir = fs::temp_directory_path() / "openads_append_qto";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED32 old_cycle = 100;
    UNSIGNED16 old_retry = 10;
    {
        const auto sp = dir.string();
        std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
        srv.push_back(0);
        ADSHANDLE hSetup = 0;
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hSetup) == 0);
        REQUIRE(AdsGetLockCycle(hSetup, &old_cycle) == 0);
        REQUIRE(AdsGetLockRetryCount(hSetup, &old_retry) == 0);
        REQUIRE(AdsSetLockCycle(hSetup, 20) == 0);
        REQUIRE(AdsSetLockRetryCount(hSetup, 3) == 0);  // budget 60 ms

        UNSIGNED8 def[] = "NAME,C,10,0";
        UNSIGNED8 tname[] = "qto.dbf";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hSetup, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hSetup) == 0);
    }

    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hA = 0, hB = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hA) == 0);
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hB) == 0);
    UNSIGNED8 tname[] = "qto.dbf";
    ADSHANDLE tA = 0, tB = 0;
    REQUIRE(AdsOpenTable(hA, tname, nullptr, ADS_CDX,
                         0, 0, 0, ADS_SHARED, &tA) == 0);
    REQUIRE(AdsOpenTable(hB, tname, nullptr, ADS_CDX,
                         0, 0, 0, ADS_SHARED, &tB) == 0);
    REQUIRE(AdsLockTable(tA) == 0);

    const auto t0 = std::chrono::steady_clock::now();
    const UNSIGNED32 rc = AdsAppendRecord(tB);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    CHECK(rc == openads::AE_LOCKED);
    CHECK(ms < 2000);

    UNSIGNED32 nrec = 99;
    REQUIRE(AdsGetRecordCount(tB, ADS_IGNOREFILTERS, &nrec) == 0);
    CHECK(nrec == 0);

    REQUIRE(AdsUnlockTable(tA) == 0);
    REQUIRE(AdsCloseTable(tA) == 0);
    REQUIRE(AdsCloseTable(tB) == 0);
    REQUIRE(AdsDisconnect(hA) == 0);
    REQUIRE(AdsDisconnect(hB) == 0);

    ADSHANDLE hRest = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hRest) == 0);
    REQUIRE(AdsSetLockCycle(hRest, old_cycle) == 0);
    REQUIRE(AdsSetLockRetryCount(hRest, old_retry) == 0);
    REQUIRE(AdsDisconnect(hRest) == 0);
    fs::remove_all(dir, ec);
}
