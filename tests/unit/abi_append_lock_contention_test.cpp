// Shared-mode AdsAppendRecord must auto-lock the new recno even under
// contention, so the following field puts cannot leave a durable blank
// (B_BIG N=700 left 580 INS-empty rows).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("remote contended append auto-locks and fills every NAME") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_append_lock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    {
        const auto sp = dir.string();
        std::vector<UNSIGNED8> s(sp.begin(), sp.end());
        s.push_back(0);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(s.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 def[] = "NAME,C,19,0;INS,N,4,0";
        UNSIGNED8 tname[] = "app.dbf";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hConn) == 0);
    }

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();

    const int workers = 32;
    const int per = 10;
    std::atomic<int> unlocked_append{0};
    std::atomic<int> append_fail{0};
    std::atomic<int> field_fail{0};
    std::atomic<int> ok{0};

    auto worker = [&](int id) {
        std::vector<UNSIGNED8> u(uri.begin(), uri.end());
        u.push_back(0);
        ADSHANDLE hConn = 0;
        if (AdsConnect60(u.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) != 0) {
            ++append_fail;
            return;
        }
        UNSIGNED8 tname[] = "app.dbf";
        ADSHANDLE hT = 0;
        if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0,
                         ADS_SHARED, &hT) != 0) {
            ++append_fail;
            AdsDisconnect(hConn);
            return;
        }
        UNSIGNED8 fName[] = "NAME";
        UNSIGNED8 fIns[] = "INS";
        for (int r = 0; r < per; ++r) {
            UNSIGNED32 rc = AdsAppendRecord(hT);
            if (rc != 0) {
                ++append_fail;
                break;
            }
            UNSIGNED16 locked = 0;
            if (AdsIsRecordLocked(hT, 0, &locked) != 0 || locked == 0) {
                ++unlocked_append;
            }
            char name[20];
            std::snprintf(name, sizeof(name), "W%02d-R%02d", id, r);
            char ins[8];
            std::snprintf(ins, sizeof(ins), "%d", id);
            if (AdsSetString(hT, fName, (UNSIGNED8*)name,
                             (UNSIGNED32)std::strlen(name)) != 0 ||
                AdsSetString(hT, fIns, (UNSIGNED8*)ins,
                             (UNSIGNED32)std::strlen(ins)) != 0 ||
                AdsWriteRecord(hT) != 0) {
                ++field_fail;
                break;
            }
            AdsUnlockRecord(hT, 0);
            ++ok;
        }
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    };

    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (int i = 0; i < workers; ++i) pool.emplace_back(worker, i);
    for (auto& t : pool) t.join();
    srv.stop();

    CHECK(unlocked_append.load() == 0);
    CHECK(field_fail.load() == 0);
    CHECK(ok.load() == workers * per);

    const auto dbf = dir / "app.dbf";
    std::ifstream in(dbf, std::ios::binary);
    REQUIRE(in.good());
    std::uint8_t hdr[32] = {};
    in.read(reinterpret_cast<char*>(hdr), 32);
    const std::uint32_t nrec =
        (std::uint32_t)hdr[4] | ((std::uint32_t)hdr[5] << 8) |
        ((std::uint32_t)hdr[6] << 16) | ((std::uint32_t)hdr[7] << 24);
    const std::uint16_t hlen =
        (std::uint16_t)(hdr[8] | ((std::uint16_t)hdr[9] << 8));
    const std::uint16_t rlen =
        (std::uint16_t)(hdr[10] | ((std::uint16_t)hdr[11] << 8));
    CHECK(nrec == (std::uint32_t)(workers * per));
    in.seekg(hlen);
    std::vector<char> rec(rlen);
    int blank = 0;
    for (std::uint32_t i = 0; i < nrec; ++i) {
        in.read(rec.data(), rlen);
        bool empty = true;
        for (int c = 1; c <= 19 && c < rlen; ++c) {
            if (rec[c] != ' ') { empty = false; break; }
        }
        if (empty) ++blank;
    }
    CHECK(blank == 0);
    fs::remove_all(dir, ec);
}
