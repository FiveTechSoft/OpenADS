// Repro attempt for Tim's report (v1.8.13): an app holding ~12 remote tables
// (each with a production CDX and an .fpt memo) hangs when closing them all
// at the end of an invoice, and even before 1.8.13 the close pass took 4-7 s.
//
// Mimics the rddads shape: open, resolve tag to index handle, navigate on the
// index handle (GotoTop + skips, which primes ordered read-ahead and the
// server-side ABI shadow handle), do some invoice-ish writes (lock, field +
// memo write, append), then close all 12 tables and disconnect, timing each
// step.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kTables = 12;
constexpr int kRows   = 50;

void make_invoice_tables(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    for (int t = 1; t <= kTables; ++t) {
        char tname[32], bag[32];
        std::snprintf(tname, sizeof(tname), "INV%02d.DBF", t);
        std::snprintf(bag,   sizeof(bag),   "INV%02d.CDX", t);
        UNSIGNED8 def[] = "ID,N,8,0;NAME,C,20;NOTES,Memo,10";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn,
                    reinterpret_cast<UNSIGNED8*>(tname), nullptr, ADS_CDX,
                    0, 0, 0, 0, def, &hT) == 0);
        UNSIGNED8 f_id[]   = "ID";
        UNSIGNED8 f_name[] = "NAME";
        UNSIGNED8 f_note[] = "NOTES";
        for (int k = 1; k <= kRows; ++k) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            AdsSetDouble(hT, f_id, static_cast<double>(k));
            AdsSetString(hT, f_name,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>("row")), 3);
            if (k % 3 == 0) {
                AdsSetString(hT, f_note,
                    reinterpret_cast<UNSIGNED8*>(const_cast<char*>(
                        "memo body long enough to hit the fpt")), 37);
            }
            REQUIRE(AdsWriteRecord(hT) == 0);
        }
        // Delete a couple of rows so SET DELETED ON has something to hide.
        REQUIRE(AdsGotoRecord(hT, 4) == 0);
        REQUIRE(AdsLockRecord(hT, 4) == 0);
        REQUIRE(AdsDeleteRecord(hT) == 0);
        REQUIRE(AdsUnlockRecord(hT, 4) == 0);

        ADSHANDLE hIndex = 0;
        UNSIGNED8 tag[]  = "BYID";
        UNSIGNED8 expr[] = "ID";
        REQUIRE(AdsCreateIndex61(hT,
                    reinterpret_cast<UNSIGNED8*>(bag), tag, expr,
                    nullptr, nullptr, ADS_COMPOUND, 512, &hIndex) == 0);
        REQUIRE(AdsCloseTable(hT) == 0);
    }
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

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
}

} // namespace

TEST_CASE("remote close of 12 ordered tables with memos completes promptly") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_close_hang";
    make_invoice_tables(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    ADSHANDLE hConn = remote_connect(dir, srv.port());

    REQUIRE(AdsShowDeleted(0) == 0);          // SET DELETED ON

    ADSHANDLE hTable[kTables] = {};
    ADSHANDLE hIndex[kTables] = {};
    auto t_open = std::chrono::steady_clock::now();
    for (int t = 0; t < kTables; ++t) {
        char tname[32], alias[16];
        std::snprintf(tname, sizeof(tname), "INV%02d.DBF", t + 1);
        std::snprintf(alias, sizeof(alias), "INV%02d", t + 1);
        REQUIRE(AdsOpenTable(hConn,
                    reinterpret_cast<UNSIGNED8*>(tname),
                    reinterpret_cast<UNSIGNED8*>(alias),
                    ADS_CDX, 0, 0, 0, 0, &hTable[t]) == 0);
        UNSIGNED8 tag[] = "BYID";
        REQUIRE(AdsGetIndexHandle(hTable[t], tag, &hIndex[t]) == 0);
    }
    std::printf("[close-hang] open x%d: %.1f ms\n", kTables, ms_since(t_open));

    // Invoice work: ordered navigation + a write + an append per table.
    auto t_work = std::chrono::steady_clock::now();
    for (int t = 0; t < kTables; ++t) {
        // Scope the order, as Tim's app does (the whole SET DELETED saga
        // runs under index scopes).
        double lo = 1.0, hi = 40.0;
        REQUIRE(AdsSetScope(hIndex[t], ADS_TOP,
                    reinterpret_cast<UNSIGNED8*>(&lo), 8, ADS_DOUBLEKEY) == 0);
        REQUIRE(AdsSetScope(hIndex[t], ADS_BOTTOM,
                    reinterpret_cast<UNSIGNED8*>(&hi), 8, ADS_DOUBLEKEY) == 0);
        REQUIRE(AdsGotoTop(hIndex[t]) == 0);
        for (int s = 0; s < 10; ++s) REQUIRE(AdsSkip(hIndex[t], 1) == 0);

        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hTable[t], 0, &rn) == 0);
        REQUIRE(AdsLockRecord(hTable[t], rn) == 0);
        UNSIGNED8 f_name[] = "NAME";
        UNSIGNED8 f_note[] = "NOTES";
        REQUIRE(AdsSetString(hTable[t], f_name,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>("edited")), 6) == 0);
        REQUIRE(AdsSetString(hTable[t], f_note,
            reinterpret_cast<UNSIGNED8*>(const_cast<char*>(
                "updated memo text after the edit")), 32) == 0);
        REQUIRE(AdsWriteRecord(hTable[t]) == 0);
        REQUIRE(AdsUnlockRecord(hTable[t], rn) == 0);

        REQUIRE(AdsAppendRecord(hTable[t]) == 0);
        UNSIGNED8 f_id[] = "ID";
        REQUIRE(AdsSetDouble(hTable[t], f_id, 1000.0 + t) == 0);
        REQUIRE(AdsWriteRecord(hTable[t]) == 0);

        // Leave the cursor mid-index with a primed read-ahead queue, the
        // state an xbrowse leaves behind.
        REQUIRE(AdsGotoTop(hIndex[t]) == 0);
        for (int s = 0; s < 5; ++s) REQUIRE(AdsSkip(hIndex[t], 1) == 0);
    }
    std::printf("[close-hang] work x%d: %.1f ms\n", kTables, ms_since(t_work));

    // A second client sits on the same tables mid-scan (multi-user), the
    // state Tim's site would typically have.
    ADSHANDLE hConn2 = remote_connect(dir, srv.port());
    ADSHANDLE hTable2[kTables] = {};
    ADSHANDLE hIndex2[kTables] = {};
    for (int t = 0; t < kTables; ++t) {
        char tname[32], alias[16];
        std::snprintf(tname, sizeof(tname), "INV%02d.DBF", t + 1);
        std::snprintf(alias, sizeof(alias), "INVB%02d", t + 1);
        REQUIRE(AdsOpenTable(hConn2,
                    reinterpret_cast<UNSIGNED8*>(tname),
                    reinterpret_cast<UNSIGNED8*>(alias),
                    ADS_CDX, 0, 0, 0, 0, &hTable2[t]) == 0);
        UNSIGNED8 tag[] = "BYID";
        REQUIRE(AdsGetIndexHandle(hTable2[t], tag, &hIndex2[t]) == 0);
        REQUIRE(AdsGotoTop(hIndex2[t]) == 0);
        for (int s = 0; s < 3; ++s) REQUIRE(AdsSkip(hIndex2[t], 1) == 0);
        // ...holding a record lock on one table, as a peer user might.
        if (t == 5) {
            UNSIGNED32 rn2 = 0;
            REQUIRE(AdsGetRecordNum(hTable2[t], 0, &rn2) == 0);
            REQUIRE(AdsLockRecord(hTable2[t], rn2) == 0);
        }
    }

    // The part Tim reports: close everything, in the exact wire order the
    // Harbour RDD emits on dbCloseArea():
    //   GOCOLD        -> AdsWriteRecord
    //   ORDLISTCLEAR  -> AdsFlushFileBuffers + AdsCloseAllIndexes
    //   CLOSE         -> AdsCloseTable
    auto t_close_all = std::chrono::steady_clock::now();
    for (int t = 0; t < kTables; ++t) {
        auto t0 = std::chrono::steady_clock::now();
        (void)AdsWriteRecord(hTable[t]);
        REQUIRE(AdsFlushFileBuffers(hTable[t]) == 0);
        REQUIRE(AdsCloseAllIndexes(hTable[t]) == 0);
        REQUIRE(AdsCloseTable(hTable[t]) == 0);
        std::printf("[close-hang] close #%02d: %.1f ms\n", t + 1, ms_since(t0));
        std::fflush(stdout);
    }
    double close_ms = ms_since(t_close_all);
    std::printf("[close-hang] close all: %.1f ms\n", close_ms);

    auto t_disc = std::chrono::steady_clock::now();
    REQUIRE(AdsDisconnect(hConn) == 0);
    std::printf("[close-hang] disconnect: %.1f ms\n", ms_since(t_disc));
    std::fflush(stdout);

    for (int t = 0; t < kTables; ++t) REQUIRE(AdsCloseTable(hTable2[t]) == 0);
    REQUIRE(AdsDisconnect(hConn2) == 0);

    // 12 loopback closes should be near-instant; anything in the seconds
    // range reproduces Tim's pre-existing delay, and a hang trips the test
    // runner's timeout.
    CHECK(close_ms < 2000.0);

    REQUIRE(AdsShowDeleted(1) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}
