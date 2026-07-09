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
    fprintf(stderr, "Create CDX (single): %8.1f ms\n", t5 - t4);
    if (hIdx) AdsCloseIndex(hIdx);

    // === Multi-tag INDEX ON simulation (like real rddads / issue #128) ===
    // Drop the bag and rebuild with several tags using AdsCreateIndex61.
    // This exercises the optimized bulk key collection path.
    std::error_code ec_rm;
    fs::remove(dir + "/bench.cdx", ec_rm);
    double t_multi = now_ms();

    // Tag 1: bare ID
    {
        UNSIGNED8 tag[] = "BY_ID";
        UNSIGNED8 expr[] = "ID";
        ADSHANDLE h = 0;
        AdsCreateIndex61(hTable, idxfile, tag, expr, nullptr, nullptr, 0, 512, &h);
        if (h) AdsCloseIndex(h);
    }
    // Tag 2: UPPER on a stringified value (simulates common UPPER usage)
    {
        UNSIGNED8 tag[] = "UP_ID";
        UNSIGNED8 expr[] = "UPPER(STR(ID,6))";
        ADSHANDLE h = 0;
        AdsCreateIndex61(hTable, idxfile, tag, expr, nullptr, nullptr, 0, 512, &h);
        if (h) AdsCloseIndex(h);
    }
    // Tag 3: numeric expression (simulates VAL-style)
    {
        UNSIGNED8 tag[] = "VAL_DBL";
        UNSIGNED8 expr[] = "VALUE*2";
        ADSHANDLE h = 0;
        AdsCreateIndex61(hTable, idxfile, tag, expr, nullptr, nullptr, 0, 512, &h);
        if (h) AdsCloseIndex(h);
    }
    // Tag 4: another bare field
    {
        UNSIGNED8 tag[] = "BY_VAL";
        UNSIGNED8 expr[] = "VALUE";
        ADSHANDLE h = 0;
        AdsCreateIndex61(hTable, idxfile, tag, expr, nullptr, nullptr, 0, 512, &h);
        if (h) AdsCloseIndex(h);
    }

    double t_multi_end = now_ms();
    fprintf(stderr, "Create CDX (4 tags): %8.1f ms  (multi-tag pattern)\n",
            t_multi_end - t_multi);

    // Explicit flush to disk
    double t6 = now_ms();
    AdsFlushFileBuffers(hTable);
    double t7 = now_ms();
    fprintf(stderr, "Flush to disk:  %8.1f ms\n", t7 - t6);

    std::error_code ec2;
    auto dbf_sz = fs::file_size(dir + "/bench.dbf", ec2);
    auto cdx_sz = fs::file_size(dir + "/bench.cdx", ec2);
    fprintf(stderr, "DBF size: %.1f MB\n", dbf_sz / (1024.0 * 1024.0));
    fprintf(stderr, "CDX size: %.1f KB\n", cdx_sz / 1024.0);

    fprintf(stderr, "\n=== TOTAL: %.1f ms ===\n", t7 - t0);

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    fs::remove_all(dir);
    return 0;
}
