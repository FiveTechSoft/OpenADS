// MANUAL BENCHMARK (skipped by default; run with --no-skip --test-case="BENCH*").
// Measures where ADT reindex time goes, to decide whether a single-scan,
// all-tags-per-pass reindex is worth changing the (CDX-shared) reindex path.
//
// Model: Table::reindex scans the whole table ONCE PER TAG. For T tags that is
//   T * (scan+decode) + T * (eval+sort+build).
// A single-scan reindex would be
//   1 * (scan+decode) + T * (eval+sort+build),
// i.e. it saves (T-1) * (scan+decode). So the decisive number is the cost of
// one full scan+decode (Tscan) relative to the whole reindex.
#include "doctest.h"
#include "openads/ace.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

namespace {
double ms_since(clk::time_point t0) {
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}
void set_c(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[32]{}; std::strncpy(reinterpret_cast<char*>(f), field, 31);
    UNSIGNED8 v[32]{}; std::strncpy(reinterpret_cast<char*>(v), val, 31);
    AdsSetString(h, f, v, static_cast<UNSIGNED32>(std::strlen(val)));
}
} // namespace

TEST_CASE("BENCH: ADT reindex scan-per-tag cost" * doctest::skip()) {
    const int N      = 200000;   // ~half of ESTAELEC; extrapolate linearly
    const int NTAGS  = 5;

    fs::path dir = fs::temp_directory_path() / "openads_reindex_bench";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // ESTAELEC-like schema (same fields the native oracle uses).
    UNSIGNED8 tbl[] = "bench.adt";
    UNSIGNED8 def[] = "CCODIGOCON,Character,3;CDOCUMETRA,Character,8;"
                      "CCODIGOCLI,Character,10;CPREFIJTRA,Character,4;"
                      "DFECTRATRA,Date,8;CCORENVELE,Character,1";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0, def, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    fs::rename(dir / "bench.adt", dir / "bench.DAT", ec); REQUIRE(!ec);
    UNSIGNED8 dat[] = "bench.DAT";
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);

    // ── Populate ─────────────────────────────────────────────────────────────
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        char con[8], doc[16], cli[16], pre[8], fec[16], cor[2];
        std::snprintf(con, sizeof(con), "%03d", i % 1000);
        std::snprintf(doc, sizeof(doc), "%08d", i);
        std::snprintf(cli, sizeof(cli), "CLIENTE%03d", i % 1000);
        std::snprintf(pre, sizeof(pre), "FA%02d", i % 100);
        std::snprintf(fec, sizeof(fec), "202601%02d", (i % 28) + 1);
        cor[0] = (i % 2) ? 'S' : 'N'; cor[1] = 0;
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        set_c(hT, "CCODIGOCON", con);
        set_c(hT, "CDOCUMETRA", doc);
        set_c(hT, "CCODIGOCLI", cli);
        set_c(hT, "CPREFIJTRA", pre);
        set_c(hT, "DFECTRATRA", fec);
        set_c(hT, "CCORENVELE", cor);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    double t_pop = ms_since(t0);

    // ── Tscan: one full physical scan+decode (no index active yet) ──────────
    t0 = clk::now();
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    long scanned = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hT, &eof);
        if (eof) break;
        UNSIGNED8 b1[16]{}, b2[16]{}; UNSIGNED32 l1 = sizeof(b1), l2 = sizeof(b2);
        AdsGetString(hT, (UNSIGNED8*)"CCODIGOCON", b1, &l1, 0);
        AdsGetString(hT, (UNSIGNED8*)"CDOCUMETRA", b2, &l2, 0);
        ++scanned;
        AdsSkip(hT, 1);
    }
    double t_scan = ms_since(t0);

    // ── Build the 5 tags (each AdsCreateIndex61 = its own full scan) ─────────
    std::string bag = (dir / "bench.adi").string();
    struct Tag { const char* name; const char* expr; const char* cond; };
    const Tag tags[NTAGS] = {
        {"ORD1", "CCODIGOCON+CDOCUMETRA", nullptr},
        {"ORD2", "CCODIGOCLI",            nullptr},
        {"ORD3", "CPREFIJTRA+CDOCUMETRA", nullptr},
        {"ORD4", "DTOS(DFECTRATRA)",      nullptr},
        {"ORD5", "DTOS(DFECTRATRA)",      "CCORENVELE != 'S'"},
    };
    double t_create_total = 0;
    for (const auto& tg : tags) {
        UNSIGNED8 b[260]{}; std::strncpy(reinterpret_cast<char*>(b), bag.c_str(), 259);
        UNSIGNED8 t[64]{};  std::strncpy(reinterpret_cast<char*>(t), tg.name, 63);
        UNSIGNED8 e[128]{}; std::strncpy(reinterpret_cast<char*>(e), tg.expr, 127);
        UNSIGNED8 c[128]{}; UNSIGNED8* cp = nullptr;
        if (tg.cond) { std::strncpy(reinterpret_cast<char*>(c), tg.cond, 127); cp = c; }
        ADSHANDLE h = 0;
        auto tc = clk::now();
        REQUIRE(AdsCreateIndex61(hT, b, t, e, cp, nullptr, 0, 0, &h) == AE_SUCCESS);
        t_create_total += ms_since(tc);
    }

    // ── T_reindex: the real Table::reindex (scans the table once PER tag) ────
    t0 = clk::now();
    REQUIRE(AdsReindex(hT) == AE_SUCCESS);
    double t_reindex = ms_since(t0);

    // ── Report + projection ──────────────────────────────────────────────────
    double saved   = (NTAGS - 1) * t_scan;             // single-scan saves (T-1) scans
    double proj    = t_reindex - saved;
    std::fprintf(stderr,
        "\n===== REINDEX BENCH (N=%d rows, %d tags) =====\n"
        "  populate           : %8.0f ms\n"
        "  Tscan (1 pass)     : %8.0f ms   (%.1f %% of reindex)\n"
        "  create 5 tags total: %8.0f ms\n"
        "  AdsReindex (N-scan): %8.0f ms\n"
        "  -- projection --\n"
        "  single-scan saves  : %8.0f ms   ((T-1) x Tscan)\n"
        "  projected 1-scan   : %8.0f ms   (%.0f %% of current)\n"
        "  extrapolated x2.2 (->441k): reindex %.1f s -> 1-scan %.1f s\n"
        "================================================\n",
        N, NTAGS, t_pop, t_scan, 100.0 * t_scan / t_reindex,
        t_create_total, t_reindex, saved, proj, 100.0 * proj / t_reindex,
        t_reindex * 2.2 / 1000.0, proj * 2.2 / 1000.0);
    std::fflush(stderr);

    CHECK(scanned == N);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}

// Where does the ~1 ms/record write cost come from? Breaks the ADT write path
// into append / set-fields / rewrite / index-maintenance / delete / PACK so we
// can see which one dominates the GUI reindex (4 min vs SAP 2 min).
TEST_CASE("BENCH: ADT write-path breakdown" * doctest::skip()) {
    const int N = 30000;

    auto make_table = [&](ADSHANDLE hConn, const char* stem) -> ADSHANDLE {
        UNSIGNED8 tbl[64]; std::snprintf(reinterpret_cast<char*>(tbl), 64, "%s.adt", stem);
        UNSIGNED8 def[] = "CCODIGOCON,Character,3;CDOCUMETRA,Character,8;"
                          "CCODIGOCLI,Character,10;CPREFIJTRA,Character,4;"
                          "DFECTRATRA,Date,8;CCORENVELE,Character,1";
        ADSHANDLE h = 0;
        REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0, def, &h)
                == AE_SUCCESS);
        return h;
    };
    auto fill = [&](ADSHANDLE h, int i) {
        char con[8], doc[16], cli[16], pre[8], fec[16], cor[2];
        std::snprintf(con, sizeof(con), "%03d", i % 1000);
        std::snprintf(doc, sizeof(doc), "%08d", i);
        std::snprintf(cli, sizeof(cli), "CLIENTE%03d", i % 1000);
        std::snprintf(pre, sizeof(pre), "FA%02d", i % 100);
        std::snprintf(fec, sizeof(fec), "202601%02d", (i % 28) + 1);
        cor[0] = (i % 2) ? 'S' : 'N'; cor[1] = 0;
        set_c(h, "CCODIGOCON", con); set_c(h, "CDOCUMETRA", doc);
        set_c(h, "CCODIGOCLI", cli); set_c(h, "CPREFIJTRA", pre);
        set_c(h, "DFECTRATRA", fec); set_c(h, "CCORENVELE", cor);
    };

    fs::path dir = fs::temp_directory_path() / "openads_writebench";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
    UNSIGNED8 srv[260]{}; std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    // (1) append-only: AdsAppendRecord + AdsWriteRecord, no field sets.
    ADSHANDLE a = make_table(hConn, "appendonly");
    auto t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        REQUIRE(AdsAppendRecord(a) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(a)  == AE_SUCCESS);
    }
    double t_append = ms_since(t0);

    // (2) full populate: append + 6 set-fields + write.
    ADSHANDLE b = make_table(hConn, "populate");
    t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        REQUIRE(AdsAppendRecord(b) == AE_SUCCESS);
        fill(b, i);
        REQUIRE(AdsWriteRecord(b) == AE_SUCCESS);
    }
    double t_pop = ms_since(t0);

    // (3) rewrite one field in place, NO index bound.
    t0 = clk::now();
    REQUIRE(AdsGotoTop(b) == AE_SUCCESS);
    for (int i = 0; i < N; ++i) {
        set_c(b, "CCODIGOCLI", "REWRITTEN0");
        REQUIRE(AdsWriteRecord(b) == AE_SUCCESS);
        AdsSkip(b, 1);
    }
    double t_rewrite_noidx = ms_since(t0);

    // (4) delete half + PACK (the DELETE-ALL-FOR + copy-down the GUI reindex does).
    t0 = clk::now();
    REQUIRE(AdsGotoTop(b) == AE_SUCCESS);
    for (int i = 0; i < N; ++i) {
        if (i % 2 == 0) AdsDeleteRecord(b);
        AdsSkip(b, 1);
    }
    double t_delete = ms_since(t0);
    t0 = clk::now();
    REQUIRE(AdsPackTable(b) == AE_SUCCESS);
    double t_pack = ms_since(t0);
    REQUIRE(AdsCloseTable(b) == AE_SUCCESS);

    // (5) rewrite one INDEXED field WITH 5 tags bound (incremental maintenance).
    ADSHANDLE c = make_table(hConn, "withidx");
    for (int i = 0; i < N; ++i) {
        REQUIRE(AdsAppendRecord(c) == AE_SUCCESS); fill(c, i);
        REQUIRE(AdsWriteRecord(c) == AE_SUCCESS);
    }
    std::string bag = (dir / "withidx.adi").string();
    const char* exprs[5] = {"CCODIGOCON+CDOCUMETRA","CCODIGOCLI","CPREFIJTRA+CDOCUMETRA",
                            "DTOS(DFECTRATRA)","DTOS(DFECTRATRA)"};
    const char* names[5] = {"ORD1","ORD2","ORD3","ORD4","ORD5"};
    for (int k = 0; k < 5; ++k) {
        UNSIGNED8 bb[260]{}; std::strncpy(reinterpret_cast<char*>(bb), bag.c_str(), 259);
        UNSIGNED8 tt[64]{};  std::strncpy(reinterpret_cast<char*>(tt), names[k], 63);
        UNSIGNED8 ee[128]{}; std::strncpy(reinterpret_cast<char*>(ee), exprs[k], 127);
        ADSHANDLE h = 0;
        REQUIRE(AdsCreateIndex61(c, bb, tt, ee, nullptr, nullptr, 0, 0, &h) == AE_SUCCESS);
    }
    t0 = clk::now();
    REQUIRE(AdsGotoTop(c) == AE_SUCCESS);
    for (int i = 0; i < N; ++i) {
        char doc[16]; std::snprintf(doc, sizeof(doc), "%08d", i + N);  // change a key field
        set_c(c, "CDOCUMETRA", doc);
        REQUIRE(AdsWriteRecord(c) == AE_SUCCESS);
        AdsSkip(c, 1);
    }
    double t_rewrite_idx = ms_since(t0);
    REQUIRE(AdsCloseTable(c) == AE_SUCCESS);

    // (6) append+write with DEFERRED FLUSH (no per-record fsync) + 1 final sync.
    ADSHANDLE d = make_table(hConn, "deferred");
    AdsSetDeferredFlush(d, 1);
    t0 = clk::now();
    for (int i = 0; i < N; ++i) {
        REQUIRE(AdsAppendRecord(d) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(d)  == AE_SUCCESS);
    }
    AdsFlushFileBuffers(d);
    double t_append_deferred = ms_since(t0);
    REQUIRE(AdsCloseTable(d) == AE_SUCCESS);

    auto us = [&](double ms_total) { return 1000.0 * ms_total / N; };
    std::fprintf(stderr,
        "\n===== ADT WRITE-PATH BREAKDOWN (N=%d) =====\n"
        "  (1) append+write          : %8.0f ms  (%6.1f us/rec)\n"
        "  (6) append+write DEFERRED  : %8.0f ms  (%6.1f us/rec)  [no per-record fsync]\n"
        "  (2) populate (6 fields)    : %8.0f ms  (%6.1f us/rec)\n"
        "      -> set 6 fields        : %8.0f ms  (%6.1f us/rec)\n"
        "  (3) rewrite, no index      : %8.0f ms  (%6.1f us/rec)\n"
        "  (5) rewrite, 5 indexes     : %8.0f ms  (%6.1f us/rec)\n"
        "      -> index maintenance   : %8.0f ms  (%6.1f us/rec)  [5 tags: erase+insert]\n"
        "  (4) delete half            : %8.0f ms  (%6.1f us/rec)\n"
        "      PACK (copy-down N/2)   : %8.0f ms  (%6.1f us/surv)\n"
        "===========================================\n",
        N,
        t_append, us(t_append),
        t_append_deferred, us(t_append_deferred),
        t_pop, us(t_pop),
        t_pop - t_append, us(t_pop - t_append),
        t_rewrite_noidx, us(t_rewrite_noidx),
        t_rewrite_idx, us(t_rewrite_idx),
        t_rewrite_idx - t_rewrite_noidx, us(t_rewrite_idx - t_rewrite_noidx),
        t_delete, us(t_delete),
        t_pack, 1000.0 * t_pack / (N / 2));
    std::fflush(stderr);

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
