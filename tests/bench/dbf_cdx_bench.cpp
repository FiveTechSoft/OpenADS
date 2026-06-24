// Benchmark: create DBF with N records + CDX index
// Uses deferred flush for bulk-insert speed, then explicit flush at end.
#include "openads/ace.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(
        steady_clock::now().time_since_epoch()).count();
}

int main(int argc, char** argv) {
    setbuf(stderr, NULL);

    int N = 500000;
    if (argc > 1) N = atoi(argv[1]);
    if (N < 1) N = 500000;

    const std::string dir = (fs::temp_directory_path() / "openads_bench").string();
    fs::create_directories(dir);
    fs::remove(dir + "/bench.dbf");
    fs::remove(dir + "/bench.cdx");

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.c_str(), dir.size());
    ADSHANDLE hConn = 0;
    AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn);

    UNSIGNED8 tblname[] = "bench.dbf";
    UNSIGNED8 flddef[] = "ID,Numeric,10;VALUE,Numeric,12,2";
    ADSHANDLE hTable = 0;

    double t0 = now_ms();
    AdsCreateTable(hConn, tblname, nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, flddef, &hTable);
    fprintf(stderr, "Create table:    %8.1f ms\n", now_ms() - t0);

    // Enable deferred flush — AdsWriteRecord will skip FlushFileBuffers
    AdsSetDeferredFlush(hTable, 1);

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> val(-10000.0, 10000.0);

    UNSIGNED8 fld_id[]    = "ID";
    UNSIGNED8 fld_value[] = "VALUE";

    fprintf(stderr, "Appending %d records (deferred flush)...\n", N);
    double t2 = now_ms();
    for (int i = 1; i <= N; ++i) {
        AdsAppendRecord(hTable);
        AdsSetDouble(hTable, fld_id, (double)i);
        AdsSetDouble(hTable, fld_value, val(rng));
        AdsWriteRecord(hTable);

        if (i % 50000 == 0) {
            double el = now_ms() - t2;
            fprintf(stderr, "  ... %7d / %d  (%.0f ms, %.0f rec/s)\n",
                    i, N, el, i / (el / 1000.0));
        }
    }
    double t3 = now_ms();
    double elapsed = t3 - t2;
    fprintf(stderr, "Append %d recs: %8.1f ms  (%.0f rec/s)\n",
            N, elapsed, N / (elapsed / 1000.0));

    // Disable deferred flush and create CDX index
    AdsSetDeferredFlush(hTable, 0);

    UNSIGNED8 idxfile[] = "bench.cdx";
    UNSIGNED8 idxname[] = "by_id";
    UNSIGNED8 idxexpr[] = "ID";
    ADSHANDLE hIdx = 0;

    double t4 = now_ms();
    AdsCreateIndex(hTable, idxfile, idxname, idxexpr, nullptr, 0, 0, &hIdx);
    double t5 = now_ms();
    fprintf(stderr, "Create CDX:     %8.1f ms\n", t5 - t4);
    if (hIdx) AdsCloseIndex(hIdx);

    // Explicit flush to disk
    double t6 = now_ms();
    AdsFlushFileBuffers(hTable);
    double t7 = now_ms();
    fprintf(stderr, "Flush to disk:  %8.1f ms\n", t7 - t6);

    std::error_code ec;
    auto dbf_sz = fs::file_size(dir + "/bench.dbf", ec);
    auto cdx_sz = fs::file_size(dir + "/bench.cdx", ec);
    fprintf(stderr, "DBF size: %.1f MB\n", dbf_sz / (1024.0 * 1024.0));
    fprintf(stderr, "CDX size: %.1f KB\n", cdx_sz / 1024.0);

    fprintf(stderr, "\n=== TOTAL: %.1f ms ===\n", t7 - t0);

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    fs::remove_all(dir);
    return 0;
}
