// Remote benchmark: 500K records over tcp:// to iMac server
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

    const char* host = "192.168.18.184";
    int port = 6262;
    const char* dir = "//home/antonio/OpenADS/bench_data";
    if (argc > 2) host = argv[2];
    if (argc > 3) port = atoi(argv[3]);
    if (argc > 4) dir = argv[4];

    char uri[256];
    snprintf(uri, sizeof(uri), "tcp://%s:%d%s", host, port, dir);
    fprintf(stderr, "Connecting to %s ...\n", uri);

    double t_total0 = now_ms();

    ADSHANDLE hConn = 0;
    AdsConnect60((UNSIGNED8*)uri, ADS_REMOTE_SERVER, NULL, NULL, 0, &hConn);
    if (!hConn) {
        fprintf(stderr, "FATAL: AdsConnect60 failed\n");
        return 1;
    }
    fprintf(stderr, "Connected (%.1f ms)\n", now_ms() - t_total0);

    // Create table on remote server
    UNSIGNED8 tblname[] = "bench500k.dbf";
    UNSIGNED8 flddef[] = "ID,Numeric,10;VALUE,Numeric,12,2";
    ADSHANDLE hTable = 0;

    double t1 = now_ms();
    AdsCreateTable(hConn, tblname, NULL, ADS_CDX, ADS_ANSI, 0, 0, 0, flddef, &hTable);
    fprintf(stderr, "Create table:    %8.1f ms\n", now_ms() - t1);

    // Enable deferred flush
    AdsSetDeferredFlush(hTable, 1);

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> val(-10000.0, 10000.0);

    UNSIGNED8 fld_id[]    = "ID";
    UNSIGNED8 fld_value[] = "VALUE";

    fprintf(stderr, "Appending %d records (deferred flush, remote)...\n", N);
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

    // Disable deferred flush and create CDX
    AdsSetDeferredFlush(hTable, 0);

    UNSIGNED8 idxfile[] = "bench500k.cdx";
    UNSIGNED8 idxname[] = "by_id";
    UNSIGNED8 idxexpr[] = "ID";
    ADSHANDLE hIdx = 0;

    double t4 = now_ms();
    AdsCreateIndex(hTable, idxfile, idxname, idxexpr, NULL, 0, 0, &hIdx);
    double t5 = now_ms();
    fprintf(stderr, "Create CDX:     %8.1f ms\n", t5 - t4);
    if (hIdx) AdsCloseIndex(hIdx);

    // Flush
    double t6 = now_ms();
    AdsFlushFileBuffers(hTable);
    double t7 = now_ms();
    fprintf(stderr, "Flush to disk:  %8.1f ms\n", t7 - t6);

    fprintf(stderr, "\n=== TOTAL: %.1f ms ===\n", t7 - t_total0);

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    return 0;
}
