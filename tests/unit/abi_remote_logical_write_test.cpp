// Remote AdsSetLogical must store the DBF logical byte 'T'/'F' on disk.
// Mirrors a Vouch login table (USERID C10, LOGGED L) whose LOGGED field
// stayed blank / non-standard after being set over REMOTE. Covers the
// plain-table path and the indexed (twin handle, conditional tag) path.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE rl_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

// Raw LOGGED byte of record `rec` (1-based), read straight from the file.
char rl_raw_logged(const fs::path& dbf, std::uint32_t rec) {
    std::ifstream f(dbf, std::ios::binary);
    REQUIRE(f.good());
    std::vector<unsigned char> h(32);
    f.read(reinterpret_cast<char*>(h.data()), 32);
    const std::uint32_t hlen = h[8] | (h[9] << 8);
    const std::uint32_t rlen = h[10] | (h[11] << 8);
    f.seekg(static_cast<std::streamoff>(hlen + (rec - 1) * rlen + 1 + 10));
    char c = '?';
    f.read(&c, 1);
    return c;
}

void rl_set_logged(ADSHANDLE hT, std::uint32_t rec, bool v) {
    UNSIGNED8 fL[] = "LOGGED";
    REQUIRE(AdsGotoRecord(hT, rec) == 0);
    REQUIRE(AdsLockRecord(hT, rec) == 0);
    CHECK(AdsSetLogical(hT, fL, v ? 1 : 0) == 0);
    CHECK(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsUnlockRecord(hT, rec) == 0);
}

struct RlFixture {
    fs::path dir;
    openads::network::Server srv;
    ADSHANDLE hConn = 0;
    ADSHANDLE hT = 0;
    UNSIGNED8 name[16] = "logina.dbf";

    explicit RlFixture(const char* sub) {
        dir = fs::temp_directory_path() / sub;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        hConn = rl_connect(dir, srv.port());
        UNSIGNED8 def[] = "USERID,C,10,0;LOGGED,L,1,0";
        REQUIRE(AdsCreateTable(hConn, name, nullptr, ADS_CDX, ADS_ANSI,
                               0, 0, 0, def, &hT) == 0);
        UNSIGNED8 fU[] = "USERID";
        const char* ids[] = {"SUPER00002", "TRAVL00002", "TVL-100002"};
        for (const char* id : ids) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, fU,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(id)), 10) == 0);
            REQUIRE(AdsWriteRecord(hT) == 0);
        }
        REQUIRE(AdsCloseTable(hT) == 0);
        hT = 0;
        REQUIRE(AdsOpenTable(hConn, name, name, ADS_CDX, ADS_ANSI,
                             ADS_COMPATIBLE_LOCKING, 0, ADS_SHARED, &hT) == 0);
    }
    void close_table() { if (hT) { AdsCloseTable(hT); hT = 0; } }
    ~RlFixture() {
        close_table();
        if (hConn) AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("remote AdsSetLogical stores T/F bytes (no index)") {
    RlFixture fx("openads_remote_logical_plain");
    rl_set_logged(fx.hT, 1, true);
    rl_set_logged(fx.hT, 2, false);
    fx.close_table();
    const fs::path dbf = fx.dir / "logina.dbf";
    const char b1 = rl_raw_logged(dbf, 1);
    const char b2 = rl_raw_logged(dbf, 2);
    const char b3 = rl_raw_logged(dbf, 3);
    INFO("raw LOGGED bytes: rec1=0x" << std::hex << int(static_cast<unsigned char>(b1))
         << " rec2=0x" << int(static_cast<unsigned char>(b2))
         << " rec3=0x" << int(static_cast<unsigned char>(b3)));
    CHECK(b1 == 'T');
    CHECK(b2 == 'F');
    CHECK(b3 == ' ');
}

TEST_CASE("remote AdsSetLogical stores T/F and maintains FOR tags (indexed)") {
    RlFixture fx("openads_remote_logical_idx");
    UNSIGNED8 bag[] = "logina.cdx";
    UNSIGNED8 tIn[] = "LOGIN";
    UNSIGNED8 tOut[] = "LOGOUT";
    UNSIGNED8 expr[] = "USERID";
    UNSIGNED8 cIn[] = "LOGGED";
    UNSIGNED8 cOut[] = ".NOT. LOGGED";
    ADSHANDLE hIn = 0, hOut = 0;
    REQUIRE(AdsCreateIndex61(fx.hT, bag, tIn, expr, cIn, nullptr,
                             ADS_COMPOUND, 0, &hIn) == 0);
    REQUIRE(AdsCreateIndex61(fx.hT, bag, tOut, expr, cOut, nullptr,
                             ADS_COMPOUND, 0, &hOut) == 0);
    UNSIGNED32 n = 99;
    REQUIRE(AdsGetKeyCount(hIn, ADS_RESPECTFILTERS, &n) == 0);
    CHECK(n == 0u);

    rl_set_logged(fx.hT, 2, true);

    n = 99;
    REQUIRE(AdsGetKeyCount(hIn, ADS_RESPECTFILTERS, &n) == 0);
    CHECK(n == 1u);
    n = 99;
    REQUIRE(AdsGetKeyCount(hOut, ADS_RESPECTFILTERS, &n) == 0);
    CHECK(n == 2u);

    fx.close_table();
    const char b2 = rl_raw_logged(fx.dir / "logina.dbf", 2);
    INFO("raw LOGGED byte rec2=0x" << std::hex << int(static_cast<unsigned char>(b2)));
    CHECK(b2 == 'T');
}
