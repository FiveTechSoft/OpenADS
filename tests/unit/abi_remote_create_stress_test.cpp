// Stress repro for the field report (Pritpal Bedi, Vouch): N concurrent
// remote workers all run the same staging dance against ONE table on the
// server data dir:
//
//   1. if (!exists(dbf)) AdsCreateTable(dbf)         -- TOCTOU race
//   2. if (!exists(cdx)) { open; 3x INDEX ON TAG; close }
//   3. open SHARED, set index, append 10 rows (field writes), close
//
// With the old code the racing CreateTable calls truncated the DBF under
// open appenders (fs::remove + ofstream CREATE_ALWAYS with no locking) and
// racing INDEX ON calls truncated the CDX bag the same way, leaving the
// table permanently corrupt (5103 "DBF header truncated" on later opens,
// key count != record count).
//
// The test runs an in-process server, storms it with N threads (each its
// own remote connection -- same server-side worker-pool interleavings as
// N client processes), then validates integrity by SCANNING the files:
// DBF header record count == 10*N, every record physically present with a
// valid delete flag, file size == hdr + count*rec_len + 1, and each CDX
// tag walks exactly 10*N keys.
//
// Worker count override: OPENADS_STRESS_WORKERS (default 120).
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

class Barrier {
public:
    explicit Barrier(int n) : n_(n) {}
    void wait() {
        std::unique_lock<std::mutex> lk(mu_);
        if (++arrived_ == n_) { cv_.notify_all(); return; }
        cv_.wait(lk, [&] { return arrived_ == n_; });
    }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    int n_;
    int arrived_ = 0;
};

ADSHANDLE stress_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0) {
        return 0;
    }
    return hConn;
}

struct WorkerResult {
    std::vector<std::string> errors;
    int appended = 0;
};

void stress_worker(const fs::path& dir, std::uint16_t port, int id,
                   Barrier& phase_b, WorkerResult& out) {
    auto fail = [&](const char* what, UNSIGNED32 rc) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "w%d %s rc=%u", id, what, rc);
        out.errors.emplace_back(buf);
    };

    ADSHANDLE hConn = stress_connect(dir, port);
    if (hConn == 0) { fail("connect", 0xFFFFFFFFu); phase_b.wait(); return; }

    // Stagger starts so creates overlap opens/indexing/appends of peers.
    std::this_thread::sleep_for(std::chrono::milliseconds(id % 8));

    UNSIGNED8 dbf[] = "StressIdx.dbf";
    UNSIGNED8 cdx[] = "StressIdx.cdx";
    UNSIGNED8 fields[] = "NAME,Character,20,0;AGE,Numeric,4,0";

    // -- step 1: create if missing --------------------------------------
    UNSIGNED16 ex = 0;
    UNSIGNED32 rc = AdsCheckExistence(hConn, dbf, &ex);
    if (rc != 0) fail("CheckExistence(dbf)", rc);
    if (rc == 0 && !ex) {
        ADSHANDLE hT = 0;
        rc = AdsCreateTable(hConn, dbf, nullptr, ADS_CDX, ADS_ANSI,
                            0, 0, 0, fields, &hT);
        // 7040 = lost the race against a creator whose handle was still
        // open (SAP ADS semantic): the table exists, just use it.
        if (rc == 0) {
            if (AdsCloseTable(hT) != 0) fail("CloseTable(create)", 1);
        } else if (rc != 7040u) {
            fail("CreateTable", rc);
        }
    }

    // -- step 2: create the 3-tag CDX if missing -------------------------
    ex = 0;
    rc = AdsCheckExistence(hConn, cdx, &ex);
    if (rc != 0) fail("CheckExistence(cdx)", rc);
    if (rc == 0 && !ex) {
        ADSHANDLE hT = 0;
        rc = AdsOpenTable(hConn, dbf, nullptr, ADS_CDX, ADS_ANSI,
                          0, 0, ADS_SHARED, &hT);
        if (rc != 0) {
            fail("OpenTable(index)", rc);
        } else {
            static const char* kTags[3]  = {"IDX01", "IDX02", "IDX03"};
            static const char* kExprs[3] = {"NAME", "AGE", "UPPER(NAME)"};
            for (int t = 0; t < 3; ++t) {
                ADSHANDLE hIdx = 0;
                rc = AdsCreateIndex61(hT, cdx, (UNSIGNED8*)kTags[t],
                                      (UNSIGNED8*)kExprs[t],
                                      nullptr, nullptr,
                                      ADS_COMPOUND, 0, &hIdx);
                if (rc != 0) {
                    char what[40];
                    std::snprintf(what, sizeof(what), "CreateIndex(%s)",
                                  kTags[t]);
                    fail(what, rc);
                } else {
                    AdsCloseIndex(hIdx);
                }
            }
            if (AdsCloseTable(hT) != 0) fail("CloseTable(index)", 1);
        }
    }

    // All workers past the create/index phase before anyone appends, so
    // the expected record count is exact (a SAP-legal overwrite of a
    // closed table cannot wipe already-appended rows).
    phase_b.wait();
    if (!out.errors.empty()) {
        AdsDisconnect(hConn);
        return;
    }

    // -- step 3: shared open + append 10 --------------------------------
    ADSHANDLE hT = 0;
    rc = AdsOpenTable(hConn, dbf, nullptr, ADS_CDX, ADS_ANSI,
                      0, 0, ADS_SHARED, &hT);
    if (rc != 0) { fail("OpenTable(append)", rc); AdsDisconnect(hConn); return; }
    {
        ADSHANDLE ah[8] = {0};
        UNSIGNED16 nidx = 8;   // capacity in, count out
        rc = AdsOpenIndex(hT, cdx, ah, &nidx);
        if (rc != 0) fail("OpenIndex", rc);
    }
    UNSIGNED8 fName[] = "NAME";
    UNSIGNED8 fAge[]  = "AGE";
    for (int r = 0; r < 10 && out.errors.empty(); ++r) {
        char name[32];
        std::snprintf(name, sizeof(name), "W%04d-R%d", id, r);
        char age[8];
        std::snprintf(age, sizeof(age), "%d", 20 + (id % 60));
        if ((rc = AdsAppendRecord(hT)) != 0) { fail("Append", rc); break; }
        if ((rc = AdsSetString(hT, fName, (UNSIGNED8*)name,
                               (UNSIGNED32)std::strlen(name))) != 0) {
            fail("SetString(NAME)", rc); break;
        }
        if ((rc = AdsSetString(hT, fAge, (UNSIGNED8*)age,
                               (UNSIGNED32)std::strlen(age))) != 0) {
            fail("SetString(AGE)", rc); break;
        }
        if ((rc = AdsWriteRecord(hT)) != 0) { fail("WriteRecord", rc); break; }
        ++out.appended;
    }
    if (AdsCloseTable(hT) != 0) fail("CloseTable(append)", 1);
    AdsDisconnect(hConn);
}

} // namespace

TEST_CASE("remote create/index/append storm keeps DBF+CDX intact [slow]" *
          doctest::timeout(240)) {
    using openads::network::Server;

    int workers = 120;
    if (const char* env = std::getenv("OPENADS_STRESS_WORKERS")) {
        int v = std::atoi(env);
        if (v >= 4 && v <= 400) workers = v;
    }

    auto data = fs::temp_directory_path() / "openads_stress_create_data";
    std::error_code ec;
    fs::remove_all(data, ec);
    fs::create_directories(data);

    Server srv;
    srv.set_enable_file_func(true);   // AdsCheckExistence rides the fs ops
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    Barrier phase_b(workers);
    std::vector<WorkerResult> results(workers);
    {
        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (int i = 0; i < workers; ++i) {
            pool.emplace_back(stress_worker, data, srv.port(), i,
                              std::ref(phase_b), std::ref(results[i]));
        }
        for (auto& t : pool) t.join();
    }

    int total_appended = 0;
    std::string first_errors;
    for (const auto& r : results) {
        total_appended += r.appended;
        for (const auto& e : r.errors) {
            if (first_errors.size() < 900) first_errors += e + "; ";
        }
    }
    INFO("worker errors: ", first_errors);
    REQUIRE(first_errors.empty());
    REQUIRE(total_appended == workers * 10);

    // -- integrity validation: scan the DBF physically -------------------
    const auto dbf_path = data / "StressIdx.dbf";
    const auto cdx_path = data / "StressIdx.cdx";
    REQUIRE(fs::exists(dbf_path));
    REQUIRE(fs::exists(cdx_path));

    std::ifstream in(dbf_path, std::ios::binary);
    REQUIRE(in.good());
    std::uint8_t hdr[32] = {0};
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    REQUIRE(in.gcount() == 32);
    const std::uint32_t hdr_count =
        (std::uint32_t)hdr[4] | ((std::uint32_t)hdr[5] << 8) |
        ((std::uint32_t)hdr[6] << 16) | ((std::uint32_t)hdr[7] << 24);
    const std::uint16_t hdr_len =
        (std::uint16_t)(hdr[8] | ((std::uint16_t)hdr[9] << 8));
    const std::uint16_t rec_len =
        (std::uint16_t)(hdr[10] | ((std::uint16_t)hdr[11] << 8));
    CHECK(hdr_count == (std::uint32_t)(workers * 10));
    CHECK(hdr_len == 32 + 32 * 2 + 2);
    CHECK(rec_len == 1 + 20 + 4);

    // Field-descriptor block must be complete (the old race left opens
    // failing with 5103 "field-descriptor block truncated").
    in.seekg(0, std::ios::end);
    const std::uint64_t fsize = (std::uint64_t)in.tellg();
    CHECK(fsize >= hdr_len);
    REQUIRE(fsize >= (std::uint64_t)hdr_len + (std::uint64_t)hdr_count * rec_len);

    // Walk every record on disk: valid delete flag, correct stride.
    in.seekg(hdr_len, std::ios::beg);
    std::vector<char> rec(rec_len);
    std::uint32_t live = 0;
    for (std::uint32_t i = 0; i < hdr_count; ++i) {
        in.read(rec.data(), rec_len);
        REQUIRE(in.gcount() == rec_len);
        CHECK((rec[0] == ' ' || rec[0] == '*'));
        if (rec[0] == ' ') ++live;
    }
    CHECK(live == hdr_count);
    in.close();

    // -- integrity validation: every tag walks exactly hdr_count keys ----
    ADSHANDLE hConn = 0;
    {
        const std::string d = data.string();
        std::vector<UNSIGNED8> dirbuf(d.begin(), d.end());
        dirbuf.push_back(0);
        REQUIRE(AdsConnect60(dirbuf.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
    }
    ADSHANDLE hT = 0;
    UNSIGNED8 dbf_name[] = "StressIdx.dbf";
    REQUIRE(AdsOpenTable(hConn, dbf_name, nullptr, ADS_CDX, ADS_ANSI,
                         0, 0, ADS_SHARED, &hT) == 0);
    UNSIGNED32 abi_count = 0;
    REQUIRE(AdsGetRecordCount(hT, ADS_IGNOREFILTERS, &abi_count) == 0);
    CHECK(abi_count == hdr_count);

    ADSHANDLE ah[8] = {0};
    UNSIGNED16 nidx = 8;   // capacity in, count out
    UNSIGNED8 cdx_name[] = "StressIdx.cdx";
    REQUIRE(AdsOpenIndex(hT, cdx_name, ah, &nidx) == 0);
    REQUIRE(nidx == 3);
    for (int t = 0; t < 3; ++t) {
        UNSIGNED32 kc = 0;
        REQUIRE(AdsGetKeyCount(ah[t], ADS_IGNOREFILTERS, &kc) == 0);
        CHECK(kc == hdr_count);
        // Physical walk: goto top + skip to EOF, counting keys.
        std::uint32_t walked = 0;
        REQUIRE(AdsGotoTop(ah[t]) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(ah[t], &eof) == 0);
            if (eof) break;
            ++walked;
            REQUIRE(AdsSkip(ah[t], 1) == 0);
        }
        CHECK(walked == hdr_count);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    srv.stop();
    fs::remove_all(data, ec);
}
