// OpenIndex must not report AE_TABLE_CORRUPTED (5103) or AE_INTERNAL_ERROR
// (5000) when the bag is being created or is held exclusive by a creator.
// Sharing-violation → 7040. Concurrent INDEX ON vs OpenIndex: 0 or 7040
// (retry until 0); never 5103/5000/6106.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"
#include "platform/file.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

void make_table(const fs::path& dir, const char* name) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 def[] = "NAME,C,10,0;INS,N,4,0";
    std::vector<UNSIGNED8> tname(name, name + std::strlen(name) + 1);
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname.data(), nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);
    UNSIGNED8 f[] = "NAME";
    for (int i = 0; i < 50; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "r%02d", i);
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)buf,
                             static_cast<UNSIGNED32>(std::strlen(buf))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

bool openindex_code_ok(UNSIGNED32 rc) {
    return rc == 0 || rc == openads::AE_FILE_IN_USE ||
           rc == openads::AE_NO_FILE_FOUND ||
           rc == openads::AE_NO_MATCHING_FILE ||
           rc == openads::AE_TABLE_NOT_FOUND;
}

}  // namespace

TEST_CASE("OpenIndex on exclusive-held bag returns 7040 not 5000/5103") {
    auto dir = fs::temp_directory_path() / "openads_oidx_excl";
    make_table(dir, "race.dbf");
    const auto cdx = dir / "race.cdx";

    auto held = openads::platform::File::open(
        cdx.string(), openads::platform::OpenMode::CreateExclusive);
    REQUIRE(held.has_value());

    const auto sp = dir.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 tname[] = "race.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX,
                         0, 0, 0, ADS_SHARED, &hT) == 0);
    UNSIGNED8 bag[] = "race.cdx";
    ADSHANDLE ah[8] = {0};
    UNSIGNED16 nidx = 8;
    const UNSIGNED32 rc = AdsOpenIndex(hT, bag, ah, &nidx);
    CHECK(rc == openads::AE_FILE_IN_USE);
    CHECK(rc != openads::AE_INTERNAL_ERROR);
    CHECK(rc != openads::AE_TABLE_CORRUPTED);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    held.value() = openads::platform::File{};  // release before rmdir
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("OpenIndex concurrent with CreateIndex61 never returns 5103") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_oidx_race";
    make_table(dir, "race.dbf");

    Server srv;
    srv.set_enable_file_func(true);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();

    auto connect = [&]() -> ADSHANDLE {
        std::vector<UNSIGNED8> u(uri.begin(), uri.end());
        u.push_back(0);
        ADSHANDLE h = 0;
        if (AdsConnect60(u.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &h) != 0)
            return 0;
        return h;
    };

    std::atomic<int> bad_5103{0};
    std::atomic<int> bad_5000{0};
    std::atomic<int> bad_6106{0};
    std::atomic<int> opened{0};
    std::atomic<bool> creating{true};

    std::thread creator([&] {
        ADSHANDLE hConn = connect();
        if (hConn == 0) return;
        UNSIGNED8 tname[] = "race.dbf";
        ADSHANDLE hT = 0;
        if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0,
                         ADS_SHARED, &hT) != 0) {
            AdsDisconnect(hConn);
            return;
        }
        UNSIGNED8 bag[] = "race.cdx";
        UNSIGNED8 tag[] = "IDX01";
        UNSIGNED8 expr[] = "NAME";
        ADSHANDLE hIdx = 0;
        (void)AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr,
                               ADS_COMPOUND, 0, &hIdx);
        if (hIdx) AdsCloseIndex(hIdx);
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
        creating = false;
    });

    std::vector<std::thread> openers;
    openers.reserve(8);
    for (int i = 0; i < 8; ++i) {
        openers.emplace_back([&] {
            ADSHANDLE hConn = connect();
            if (hConn == 0) return;
            UNSIGNED8 tname[] = "race.dbf";
            ADSHANDLE hT = 0;
            if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0,
                             ADS_SHARED, &hT) != 0) {
                AdsDisconnect(hConn);
                return;
            }
            UNSIGNED8 bag[] = "race.cdx";
            for (int attempt = 0; attempt < 40; ++attempt) {
                ADSHANDLE ah[8] = {0};
                UNSIGNED16 nidx = 8;
                UNSIGNED32 rc = AdsOpenIndex(hT, bag, ah, &nidx);
                if (rc == openads::AE_TABLE_CORRUPTED) ++bad_5103;
                else if (rc == openads::AE_INTERNAL_ERROR) ++bad_5000;
                else if (rc == 6106u) ++bad_6106;
                else if (rc == 0) {
                    ++opened;
                    break;
                } else if (!openindex_code_ok(rc) && creating) {
                    ++bad_5000;
                }
                if (!creating && rc != 0) {
                    // Creator finished; one more try then stop.
                    ADSHANDLE ah2[8] = {0};
                    UNSIGNED16 n2 = 8;
                    rc = AdsOpenIndex(hT, bag, ah2, &n2);
                    if (rc == 0) ++opened;
                    else if (rc == openads::AE_TABLE_CORRUPTED) ++bad_5103;
                    else if (rc == openads::AE_INTERNAL_ERROR) ++bad_5000;
                    else if (rc == 6106u) ++bad_6106;
                    break;
                }
                std::this_thread::yield();
            }
            AdsCloseTable(hT);
            AdsDisconnect(hConn);
        });
    }
    creator.join();
    for (auto& t : openers) t.join();
    srv.stop();

    CHECK(bad_5103.load() == 0);
    CHECK(bad_5000.load() == 0);
    CHECK(bad_6106.load() == 0);
    CHECK(opened.load() >= 1);
    std::error_code ec;
    fs::remove_all(dir, ec);
}
