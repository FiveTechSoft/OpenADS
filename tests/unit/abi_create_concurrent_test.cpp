// Deterministic regression for the concurrent-CreateTable fix (field
// report: Pritpal Bedi stress test corrupting TestIndex.dbf with N=200
// remote processes racing DbCreate on the same path).
//
// SAP ADS 10.10 semantics, verified against the SAP ace32.dll on
// 2026-08-30 (build/sap-probe):
//   - AdsCreateTable over an existing-but-CLOSED table succeeds and
//     overwrites it (no AE_TABLE_ALREADY_EXISTS exists in SAP ace.h).
//   - AdsCreateTable over a table another connection holds OPEN fails
//     with 7040 and the open connection's data is left intact.
//
// OpenADS used to fs::remove + ofstream-truncate unconditionally, so a
// racing create truncated the DBF underneath open appenders (permanent
// corruption) and concurrent opens read a partially-written header
// (5103 "DBF header truncated").
#include "doctest.h"
#include "openads/ace.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE local_connect(const fs::path& dir) {
    const std::string d = dir.string();
    std::vector<UNSIGNED8> srv(d.begin(), d.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        return 0;
    }
    return hConn;
}

} // namespace

TEST_CASE("AdsCreateTable over an open table fails 7040 and preserves data") {
    auto dir = fs::temp_directory_path() / "openads_create_while_open";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    ADSHANDLE hC1 = local_connect(dir);
    ADSHANDLE hC2 = local_connect(dir);
    REQUIRE(hC1 != 0);
    REQUIRE(hC2 != 0);

    UNSIGNED8 name[]   = "t1.dbf";
    UNSIGNED8 fields[] = "NAME,Character,20,0;AGE,Numeric,4,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hC1, name, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields, &hT) == 0);
    REQUIRE(AdsAppendRecord(hT) == 0);
    UNSIGNED8 fName[] = "NAME";
    UNSIGNED8 val[]   = "HELLO";
    REQUIRE(AdsSetString(hT, fName, val, 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    // Keep hT OPEN on conn1.

    // conn2 tries to create over the same path: must fail 7040, and the
    // on-disk data conn1 wrote must survive.
    ADSHANDLE hT2 = 0;
    const UNSIGNED32 rc = AdsCreateTable(hC2, name, nullptr, ADS_CDX,
                                         ADS_ANSI, 0, 0, 0, fields, &hT2);
    CHECK(rc == 7040u);
    if (hT2 != 0) AdsCloseTable(hT2);

    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hT, ADS_IGNOREFILTERS, &n) == 0);
    CHECK(n == 1u);

    REQUIRE(AdsCloseTable(hT) == 0);

    // Over a CLOSED existing table the create succeeds and overwrites
    // (SAP behaviour: create2 rc=0, row count reset).
    REQUIRE(AdsCreateTable(hC2, name, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields, &hT2) == 0);
    n = 77;
    REQUIRE(AdsGetRecordCount(hT2, ADS_IGNOREFILTERS, &n) == 0);
    CHECK(n == 0u);
    REQUIRE(AdsCloseTable(hT2) == 0);

    AdsDisconnect(hC1);
    AdsDisconnect(hC2);
    fs::remove_all(dir, ec);
}

TEST_CASE("racing AdsCreateTable calls on one path never tear the header") {
    auto dir = fs::temp_directory_path() / "openads_create_race";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    constexpr int kThreads = 16;
    std::atomic<int> start_count{0};
    std::vector<UNSIGNED32> rcs(kThreads, 0xFFFFFFFFu);

    {
        std::vector<std::thread> pool;
        pool.reserve(kThreads);
        for (int i = 0; i < kThreads; ++i) {
            pool.emplace_back([&, i] {
                ADSHANDLE hConn = local_connect(dir);
                if (hConn == 0) { rcs[i] = 0xFFFFFFFEu; return; }
                // Release all creators at once to maximise overlap.
                start_count.fetch_add(1);
                while (start_count.load() < kThreads) std::this_thread::yield();
                UNSIGNED8 name[]   = "race.dbf";
                UNSIGNED8 fields[] = "NAME,Character,20,0;AGE,Numeric,4,0";
                ADSHANDLE hT = 0;
                const UNSIGNED32 rc = AdsCreateTable(
                    hConn, name, nullptr, ADS_CDX, ADS_ANSI,
                    0, 0, 0, fields, &hT);
                rcs[i] = rc;
                if (rc == 0 && hT != 0) AdsCloseTable(hT);
                AdsDisconnect(hConn);
            });
        }
        for (auto& t : pool) t.join();
    }

    // Every racer either succeeded or got the SAP create-while-open code;
    // never 5103 (torn header) or 5000 (create failure).
    for (int i = 0; i < kThreads; ++i) {
        INFO("thread ", i, " rc=", rcs[i]);
        CHECK((rcs[i] == 0u || rcs[i] == 7040u));
    }

    // The surviving file is a well-formed table: opens cleanly, schema
    // intact, zero records.
    ADSHANDLE hConn = local_connect(dir);
    REQUIRE(hConn != 0);
    UNSIGNED8 name[] = "race.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, name, nullptr, ADS_CDX, ADS_ANSI,
                         0, 0, ADS_SHARED, &hT) == 0);
    UNSIGNED16 nflds = 0;
    REQUIRE(AdsGetNumFields(hT, &nflds) == 0);
    CHECK(nflds == 2);
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}
