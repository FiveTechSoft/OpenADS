// Multithreaded table+CDX creation: OpenADS (ACE / ADSCDX) vs Harbour DBFCDX.
//
// Policy: every ADSCDX speed measurement is paired with a Harbour DBFCDX
// baseline of the SAME workload (same N tables, same T threads, same
// 10-row + 3-tag schema). Without a DBFCDX number the test SKIPs — we
// never report an ADSCDX-only "pass" as a performance result.
//
// Workload (mirrors _mt400 / mt_create_bench.prg):
//   N tables split across T threads; each table gets 10 fixed records
//   and a compound CDX with tags IDX01(NAME)/IDX02(CITY)/IDX03(INS).
//
// Correctness: every instance ends with LastRec==10 and identical
// dbf/cdx byte sizes within a side.
//
// Speed: ADSCDX wall time is reported next to DBFCDX; a hard fail only
// if ADSCDX is more than kMaxSlowdown× slower (sanity band, not a tight
// perf gate — CI machines vary). Tag [slow] on the larger configurations.
//
// Harbour harness: tests/unit/mt_create_bench.exe (built with
// build_mt_create_bench.bat). Located via OPENADS_MT_BENCH_EXE env or
// next to this source / PATH.

#include "doctest.h"
#include "openads/ace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kRecsPerTable = 10;
// Soft band: ADSCDX must not be more than this × slower than DBFCDX.
// Historical mt400 on this machine: ADSCDX ~3–4× DBFCDX for pure create;
// 8× leaves headroom for cold cache / AV scanners without hiding a
// real single-threaded global-mutex regression (~∞×).
constexpr double kMaxSlowdown = 8.0;

struct BenchResult {
    std::string rdd;
    int         count   = 0;
    int         threads = 0;
    int64_t     ms      = -1;
    bool        ok      = false;
    int64_t     dbf_size = 0;
    int64_t     cdx_size = 0;
    std::string raw;
};

// ---------- ACE (OpenADS ADSCDX path) ------------------------------------

void connect_local(const fs::path& dir, ADSHANDLE* conn) {
    std::string d = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                         ADS_LOCAL_SERVER, nullptr, nullptr, 0, conn) == 0);
}

std::string table_name(int n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "mt%04d.dbf", n);
    return buf;
}
std::string index_name(int n) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "mt%04d.cdx", n);
    return buf;
}

// One worker: create tables [first..last] inclusive via ACE.
bool ace_make_range(const fs::path& dir, int first, int last,
                    std::atomic<int>& fail_stage,
                    std::atomic<UNSIGNED32>& fail_rc) {
    static const char* names[10]  = {"Alice", "Bob", "Phillip", "Charlie",
                                     "Linda", "Finland", "Diana", "Lucy",
                                     "Jony", "Edward"};
    static const char* cities[10] = {"Madrid", "Barcelona", "Panipat",
                                     "Valencia", "Iris", "Dallas",
                                     "Sevilla", "Sevilla", "Walker",
                                     "Bilbao"};
    ADSHANDLE conn = 0;
    std::string d = dir.string();
    if (AdsConnect60(reinterpret_cast<UNSIGNED8*>(d.data()),
                     ADS_LOCAL_SERVER, nullptr, nullptr, 0, &conn) != 0) {
        fail_stage = 1;
        return false;
    }

    for (int n = first; n <= last; ++n) {
        std::string dbf = table_name(n);
        std::string cdx = index_name(n);
        ADSHANDLE hTbl = 0, hIdx = 0;
        UNSIGNED32 rc;

        if ((rc = AdsCreateTable(
                 conn, reinterpret_cast<UNSIGNED8*>(dbf.data()), nullptr,
                 ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS, 0,
                 reinterpret_cast<UNSIGNED8*>(
                     const_cast<char*>("NAME,C,19;CITY,C,15;AGE,N,3;INS,N,4;RDD,C,3")),
                 &hTbl)) != 0) {
            fail_stage = 2;
            fail_rc    = rc;
            AdsDisconnect(conn);
            return false;
        }
        // Create leaves the table open; close + reopen exclusive before
        // appends (matches Harbour workaround for hang-after-dbCreate).
        AdsCloseTable(hTbl);
        hTbl = 0;
        if ((rc = AdsOpenTable(
                 conn, reinterpret_cast<UNSIGNED8*>(dbf.data()), nullptr,
                 ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                 ADS_EXCLUSIVE, &hTbl)) != 0) {
            fail_stage = 3;
            fail_rc    = rc;
            AdsDisconnect(conn);
            return false;
        }

        for (int i = 0; i < kRecsPerTable; ++i) {
            if ((rc = AdsAppendRecord(hTbl)) != 0) {
                fail_stage = 4;
                fail_rc    = rc;
                AdsCloseTable(hTbl);
                AdsDisconnect(conn);
                return false;
            }
            char ins[16];
            std::snprintf(ins, sizeof(ins), "%d", i + 1);
            AdsSetString(hTbl, (UNSIGNED8*)"NAME",
                         (UNSIGNED8*)names[i],
                         (UNSIGNED16)std::strlen(names[i]));
            AdsSetString(hTbl, (UNSIGNED8*)"CITY",
                         (UNSIGNED8*)cities[i],
                         (UNSIGNED16)std::strlen(cities[i]));
            AdsSetString(hTbl, (UNSIGNED8*)"INS", (UNSIGNED8*)ins,
                         (UNSIGNED16)std::strlen(ins));
            AdsSetString(hTbl, (UNSIGNED8*)"RDD", (UNSIGNED8*)"ADS", 3);
            if ((rc = AdsWriteRecord(hTbl)) != 0) {
                fail_stage = 5;
                fail_rc    = rc;
                AdsCloseTable(hTbl);
                AdsDisconnect(conn);
                return false;
            }
        }

        const char* tags[3][2] = {{"IDX01", "NAME"},
                                  {"IDX02", "CITY"},
                                  {"IDX03", "INS"}};
        for (auto& tg : tags) {
            if ((rc = AdsCreateIndex61(
                     hTbl, reinterpret_cast<UNSIGNED8*>(cdx.data()),
                     (UNSIGNED8*)tg[0], (UNSIGNED8*)tg[1], nullptr, nullptr,
                     0, 0, &hIdx)) != 0) {
                fail_stage = 6;
                fail_rc    = rc;
                AdsCloseTable(hTbl);
                AdsDisconnect(conn);
                return false;
            }
        }
        if ((rc = AdsCloseTable(hTbl)) != 0) {
            fail_stage = 7;
            fail_rc    = rc;
            AdsDisconnect(conn);
            return false;
        }
    }
    AdsDisconnect(conn);
    return true;
}

BenchResult ace_adscdx_run(const fs::path& dir, int count, int threads) {
    BenchResult r;
    r.rdd     = "ADSCDX-ACE";
    r.count   = count;
    r.threads = threads;

    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    std::atomic<int> fail_stage{-1};
    std::atomic<UNSIGNED32> fail_rc{0};
    std::atomic<int> failed{0};

    const int per = (std::max)(1, count / threads);
    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> pool;
    int first = 1;
    for (int t = 0; t < threads; ++t) {
        if (first > count) break;
        int last = (t == threads - 1) ? count
                                      : (std::min)(count, first + per - 1);
        pool.emplace_back([&, first, last] {
            if (!ace_make_range(dir, first, last, fail_stage, fail_rc))
                failed.fetch_add(1);
        });
        first = last + 1;
    }
    for (auto& th : pool) th.join();

    auto t1 = std::chrono::steady_clock::now();
    r.ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0)
               .count();

    if (failed.load() != 0) {
        r.ok = false;
        r.raw = "ACE fail stage=" + std::to_string(fail_stage.load()) +
                " rc=" + std::to_string(fail_rc.load());
        return r;
    }

    // Verify sizes + record counts
    int64_t dbf0 = -1, cdx0 = -1;
    ADSHANDLE conn = 0;
    connect_local(dir, &conn);
    r.ok = true;
    for (int n = 1; n <= count; ++n) {
        auto dbfp = dir / table_name(n);
        auto cdxp = dir / index_name(n);
        if (!fs::exists(dbfp) || !fs::exists(cdxp)) {
            r.ok = false;
            r.raw = "missing files for inst " + std::to_string(n);
            break;
        }
        int64_t ds = static_cast<int64_t>(fs::file_size(dbfp, ec));
        int64_t cs = static_cast<int64_t>(fs::file_size(cdxp, ec));
        if (n == 1) {
            dbf0 = ds;
            cdx0 = cs;
        }
        if (ds != dbf0 || cs != cdx0 || ds <= 0 || cs <= 0) {
            r.ok = false;
            r.raw = "size mismatch inst " + std::to_string(n);
            break;
        }
        ADSHANDLE hTbl = 0;
        std::string dbf = table_name(n);
        if (AdsOpenTable(conn, reinterpret_cast<UNSIGNED8*>(dbf.data()),
                         nullptr, ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                         ADS_CHECKRIGHTS, ADS_SHARED, &hTbl) != 0) {
            r.ok = false;
            r.raw = "open failed inst " + std::to_string(n);
            break;
        }
        UNSIGNED32 cnt = 0;
        AdsGetRecordCount(hTbl, ADS_IGNOREFILTERS, &cnt);
        AdsCloseTable(hTbl);
        if (cnt != static_cast<UNSIGNED32>(kRecsPerTable)) {
            r.ok = false;
            r.raw = "rec count " + std::to_string(cnt) + " at inst " +
                    std::to_string(n);
            break;
        }
    }
    AdsDisconnect(conn);
    r.dbf_size = dbf0;
    r.cdx_size = cdx0;
    return r;
}

// ---------- Harbour subprocess (DBFCDX / ADSCDX via rddads) ---------------

fs::path find_mt_create_bench_exe() {
    if (const char* env = std::getenv("OPENADS_MT_BENCH_EXE")) {
        fs::path p(env);
        if (fs::exists(p)) return p;
    }
    // Common relative locations from the unit-test binary / source tree.
    const char* candidates[] = {
        "mt_create_bench.exe",
        "tests/unit/mt_create_bench.exe",
        "../tests/unit/mt_create_bench.exe",
        "../../tests/unit/mt_create_bench.exe",
        "C:/OpenADS/tests/unit/mt_create_bench.exe",
    };
    for (auto* c : candidates) {
        fs::path p(c);
        std::error_code ec;
        if (fs::exists(p, ec)) return fs::absolute(p);
    }
    return {};
}

#if defined(_WIN32)
bool run_process(const std::wstring& cmdline, const fs::path& cwd,
                 const std::vector<std::pair<std::wstring, std::wstring>>& env,
                 std::string& out, int& exit_code, int timeout_ms) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    // Build environment block: inherit + overrides.
    // For simplicity pass via SetEnvironmentVariable in this process
    // around CreateProcess is racy under MT tests; use a private block.
    std::wstring env_block;
    {
        // Snapshot parent env
        LPWCH parent = GetEnvironmentStringsW();
        if (parent) {
            for (LPWCH p = parent; *p; ) {
                std::wstring entry(p);
                env_block += entry;
                env_block.push_back(L'\0');
                p += entry.size() + 1;
            }
            FreeEnvironmentStringsW(parent);
        }
        for (auto& kv : env) {
            env_block += kv.first;
            env_block.push_back(L'=');
            env_block += kv.second;
            env_block.push_back(L'\0');
        }
        env_block.push_back(L'\0');
    }

    std::wstring cmd = cmdline;
    PROCESS_INFORMATION pi{};
    std::wstring cwd_w = cwd.wstring();
    // CREATE_UNICODE_ENVIRONMENT is required when lpEnvironment is a
    // UTF-16 block (otherwise CreateProcess fails with ERROR_INVALID_PARAMETER).
    BOOL ok = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        env_block.empty() ? nullptr : env_block.data(),
        cwd_w.empty() ? nullptr : cwd_w.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(rd);
        out = "CreateProcessW failed gle=" + std::to_string(err);
        return false;
    }

    std::string captured;
    char buf[4096];
    DWORD n = 0;
    auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) &&
            avail > 0) {
            DWORD to_read = (std::min)(avail, (DWORD)sizeof(buf));
            if (ReadFile(rd, buf, to_read, &n, nullptr) && n > 0)
                captured.append(buf, buf + n);
            continue;
        }
        DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            // Drain remainder
            while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n > 0)
                captured.append(buf, buf + n);
            break;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 259);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(rd);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            out = captured + "\n[timeout]";
            exit_code = 259;
            return true;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    exit_code = static_cast<int>(code);
    out       = std::move(captured);
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// Harbour MT harness is Windows-first (msvc64 hbmk2). Keep helpers inside
// the same #if so clang -Werror on Linux/macOS does not trip on unused
// stubs (release v1.8.73 POSIX legs).
BenchResult parse_harbour_result(const std::string& text) {
    BenchResult r;
    r.raw = text;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("RESULT ", 0) == 0) {
            // RESULT rdd=DBFCDX count=40 threads=4 ms=1234 ok=1 errors=0
            auto get = [&](const char* key) -> std::string {
                std::string k = std::string(key) + "=";
                auto pos = line.find(k);
                if (pos == std::string::npos) return {};
                pos += k.size();
                auto end = line.find(' ', pos);
                return line.substr(pos, end == std::string::npos
                                            ? std::string::npos
                                            : end - pos);
            };
            r.rdd     = get("rdd");
            r.count   = std::atoi(get("count").c_str());
            r.threads = std::atoi(get("threads").c_str());
            r.ms      = std::atoll(get("ms").c_str());
            r.ok      = get("ok") == "1";
        } else if (line.rfind("SIZES ", 0) == 0) {
            auto get = [&](const char* key) -> std::string {
                std::string k = std::string(key) + "=";
                auto pos = line.find(k);
                if (pos == std::string::npos) return {};
                pos += k.size();
                auto end = line.find(' ', pos);
                return line.substr(pos, end == std::string::npos
                                            ? std::string::npos
                                            : end - pos);
            };
            r.dbf_size = std::atoll(get("dbf").c_str());
            r.cdx_size = std::atoll(get("cdx").c_str());
        }
    }
    return r;
}
#endif  // _WIN32

BenchResult harbour_run(const fs::path& bench_exe, const fs::path& work,
                        const char* rdd, int count, int threads,
                        int timeout_ms = 180000) {
    BenchResult r;
    r.rdd     = rdd;
    r.count   = count;
    r.threads = threads;
    if (bench_exe.empty() || !fs::exists(bench_exe)) {
        r.ok  = false;
        r.raw = "mt_create_bench.exe not found (build with "
                "tests/unit/build_mt_create_bench.bat)";
        return r;
    }
    std::error_code ec;
    fs::create_directories(work, ec);

#if defined(_WIN32)
    // Ensure ace64.dll / openace64.dll sit next to the harness.
    fs::path harness_dir = bench_exe.parent_path();
    const char* ace_cands[] = {
        "C:/OpenADS/build/default/src/Release/openace64.dll",
        "C:/OpenADS/build/default/src/Release/ace64.dll",
    };
    for (auto* c : ace_cands) {
        if (fs::exists(c, ec)) {
            fs::copy_file(c, harness_dir / "openace64.dll",
                          fs::copy_options::overwrite_existing, ec);
            fs::copy_file(c, harness_dir / "ace64.dll",
                          fs::copy_options::overwrite_existing, ec);
            break;
        }
    }

    std::wstring cmd = L"\"" + bench_exe.wstring() + L"\"";
    std::vector<std::pair<std::wstring, std::wstring>> env = {
        {L"MT_COUNT", std::to_wstring(count)},
        {L"MT_THREADS", std::to_wstring(threads)},
        {L"USE_RDD", std::wstring(rdd, rdd + std::strlen(rdd))},
        {L"MT_DIR", work.wstring()},
    };
    std::string out;
    int code = 1;
    if (!run_process(cmd, harness_dir, env, out, code, timeout_ms)) {
        r.ok  = false;
        r.raw = "CreateProcess failed for mt_create_bench.exe";
        return r;
    }
    r = parse_harbour_result(out);
    if (r.ms < 0) {
        r.ok  = false;
        r.raw = "no RESULT line; exit=" + std::to_string(code) +
                " out=" + out;
    }
    if (code != 0) r.ok = false;
    return r;
#else
    (void)timeout_ms;
    r.ok  = false;
    r.raw = "Harbour MT bench not supported on this platform";
    return r;
#endif
}

void report_pair(const BenchResult& dbfcdx, const BenchResult& adscdx) {
    MESSAGE("DBFCDX  count=", dbfcdx.count, " threads=", dbfcdx.threads,
            " ms=", dbfcdx.ms, " ok=", dbfcdx.ok,
            " dbf=", dbfcdx.dbf_size, " cdx=", dbfcdx.cdx_size);
    MESSAGE("ADSCDX  count=", adscdx.count, " threads=", adscdx.threads,
            " ms=", adscdx.ms, " ok=", adscdx.ok,
            " dbf=", adscdx.dbf_size, " cdx=", adscdx.cdx_size,
            " side=", adscdx.rdd);
    if (dbfcdx.ms > 0 && adscdx.ms > 0) {
        double ratio = static_cast<double>(adscdx.ms) /
                       static_cast<double>(dbfcdx.ms);
        MESSAGE("SPEED   ADSCDX/DBFCDX ratio=", ratio,
                " (fail if >", kMaxSlowdown, "x)");
    }
}

void require_pair(const BenchResult& dbfcdx, const BenchResult& adscdx) {
    report_pair(dbfcdx, adscdx);
    REQUIRE_MESSAGE(dbfcdx.ok, "Harbour DBFCDX baseline failed: ", dbfcdx.raw);
    REQUIRE_MESSAGE(adscdx.ok, "ADSCDX run failed: ", adscdx.raw);
    REQUIRE(dbfcdx.ms >= 0);
    REQUIRE(adscdx.ms >= 0);
    // Correctness: same record count side-effect (sizes may differ by
    // packing; both must be > 0 and consistent within a side).
    CHECK(dbfcdx.dbf_size > 0);
    CHECK(dbfcdx.cdx_size > 0);
    CHECK(adscdx.dbf_size > 0);
    CHECK(adscdx.cdx_size > 0);
    if (dbfcdx.ms > 0) {
        double ratio = static_cast<double>(adscdx.ms) /
                       static_cast<double>(dbfcdx.ms);
        CHECK_MESSAGE(ratio <= kMaxSlowdown,
                      "ADSCDX is ", ratio,
                      "x slower than Harbour DBFCDX (cap ", kMaxSlowdown,
                      "x). DBFCDX=", dbfcdx.ms, "ms ADSCDX=", adscdx.ms, "ms");
    }
}

}  // namespace

// ---- Tests ----------------------------------------------------------------

TEST_CASE("MT create: ADSCDX-ACE vs Harbour DBFCDX (40 tables / 4 threads)") {
    auto bench = find_mt_create_bench_exe();
    if (bench.empty()) {
        MESSAGE("SKIP: build tests/unit/mt_create_bench.exe first "
                "(build_mt_create_bench.bat) — DBFCDX baseline required");
        return;
    }

    const auto root =
        fs::temp_directory_path() / "openads_mt_create_vs_dbfcdx_40";
    std::error_code ec;
    fs::remove_all(root, ec);

    const int count = 40;
    const int threads = 4;

    auto dbfcdx = harbour_run(bench, root / "dbfcdx", "DBFCDX", count, threads);
    auto adscdx = ace_adscdx_run(root / "adscdx_ace", count, threads);

    require_pair(dbfcdx, adscdx);
    fs::remove_all(root, ec);
}

TEST_CASE("MT create: Harbour ADSCDX vs Harbour DBFCDX "
          "(40 tables / 4 threads) [rddads]") {
    // Same workload through rddads on both RDDs — isolates OpenADS ACE
    // from Harbour RDD overhead and still always pairs ADSCDX with DBFCDX.
    auto bench = find_mt_create_bench_exe();
    if (bench.empty()) {
        MESSAGE("SKIP: mt_create_bench.exe not found");
        return;
    }

    const auto root =
        fs::temp_directory_path() / "openads_mt_create_hb_both_40";
    std::error_code ec;
    fs::remove_all(root, ec);

    const int count = 40;
    const int threads = 4;

    auto dbfcdx = harbour_run(bench, root / "dbfcdx", "DBFCDX", count, threads);
    auto adscdx = harbour_run(bench, root / "adscdx", "ADSCDX", count, threads);

    require_pair(dbfcdx, adscdx);
    fs::remove_all(root, ec);
}

TEST_CASE("MT create [slow]: ADSCDX-ACE vs Harbour DBFCDX "
          "(80 tables / 8 threads)") {
    auto bench = find_mt_create_bench_exe();
    if (bench.empty()) {
        MESSAGE("SKIP: mt_create_bench.exe not found");
        return;
    }

    const auto root =
        fs::temp_directory_path() / "openads_mt_create_vs_dbfcdx_80";
    std::error_code ec;
    fs::remove_all(root, ec);

    const int count = 80;
    const int threads = 8;

    auto dbfcdx = harbour_run(bench, root / "dbfcdx", "DBFCDX", count, threads);
    auto adscdx = ace_adscdx_run(root / "adscdx_ace", count, threads);

    require_pair(dbfcdx, adscdx);
    fs::remove_all(root, ec);
}

TEST_CASE("MT create [slow]: Harbour ADSCDX vs Harbour DBFCDX "
          "(80 tables / 8 threads) [rddads]") {
    auto bench = find_mt_create_bench_exe();
    if (bench.empty()) {
        MESSAGE("SKIP: mt_create_bench.exe not found");
        return;
    }

    const auto root =
        fs::temp_directory_path() / "openads_mt_create_hb_both_80";
    std::error_code ec;
    fs::remove_all(root, ec);

    const int count = 80;
    const int threads = 8;

    auto dbfcdx = harbour_run(bench, root / "dbfcdx", "DBFCDX", count, threads);
    auto adscdx = harbour_run(bench, root / "adscdx", "ADSCDX", count, threads);

    require_pair(dbfcdx, adscdx);
    fs::remove_all(root, ec);
}
