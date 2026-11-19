// the reporting user findings 2b/2c, forwarded by Pritpal Bedi, 2026-10-25.
// Tests only. Public native ACE calls mirror the reported RDD sequence.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include <cstdio>
#include <filesystem>
#include <string>

namespace {
UNSIGNED8* bc_bytes(std::string& s) { return reinterpret_cast<UNSIGNED8*>(s.data()); }
std::filesystem::path bc_dir(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / ("openads_scope_settle_" + name);
    std::error_code ec;
    std::filesystem::remove_all(p, ec);
    std::filesystem::create_directories(p);
    return p;
}
ADSHANDLE bc_connect(const std::filesystem::path& dir, unsigned port = 0) {
    std::string uri = port ? "tcp://127.0.0.1:" + std::to_string(port) + "/" + dir.generic_string() : dir.string();
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(bc_bytes(uri), port ? ADS_REMOTE_SERVER : ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, &h) == 0);
    return h;
}
std::string bc_key(unsigned width, unsigned n, bool letters) {
    char b[32];
    if (letters) std::snprintf(b, sizeof(b), "W%0*u", static_cast<int>(width - 1), n);
    else std::snprintf(b, sizeof(b), "%0*u", static_cast<int>(width), n);
    return b;
}
void bc_seed(const std::filesystem::path& dir, unsigned width, bool letters, bool indexed) {
    ADSHANDLE c = bc_connect(dir), t = 0;
    std::string name = "rows.dbf", def = "NAME,C," + std::to_string(width) + ",0";
    REQUIRE(AdsCreateTable(c, bc_bytes(name), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, bc_bytes(def), &t) == 0);
    std::string field = "NAME";
    for (unsigned n : {7u,5u,5u,9u}) {
        std::string key = bc_key(width, n, letters);
        REQUIRE(AdsAppendRecord(t) == 0);
        REQUIRE(AdsSetString(t, bc_bytes(field), bc_bytes(key), width) == 0);
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    if (indexed) {
        std::string bag = (dir / "rows.cdx").string(), tag = "BYNAME", expr = "NAME";
        ADSHANDLE idx = 0;
        REQUIRE(AdsCreateIndex61(t, bc_bytes(bag), bc_bytes(tag), bc_bytes(expr),
                                nullptr, nullptr, ADS_COMPOUND, 512, &idx) == 0);
    }
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(c) == 0);
}
ADSHANDLE bc_open(ADSHANDLE c) {
    std::string name = "rows.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(c, bc_bytes(name), nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, ADS_EXCLUSIVE, &t) == 0);
    return t;
}
}

TEST_CASE("scope settle: new tags attach immediately on an exclusive open table") {
    for (bool remote : {false, true}) {
        for (bool legacy : {false, true}) {
            INFO("remote=" << remote << " legacy API=" << legacy);
            auto dir = bc_dir(std::string("tags_") + (remote ? "remote" : "local") + (legacy ? "_legacy" : "_61"));
            bc_seed(dir, 8, false, false);
            openads::network::Server srv;
            if (remote) REQUIRE(srv.start("127.0.0.1", 0));
            ADSHANDLE c = bc_connect(dir, remote ? srv.port() : 0), t = bc_open(c);
            UNSIGNED16 count = 99;
            REQUIRE(AdsGetNumIndexes(t, &count) == 0);
            CHECK(count == 0);
            std::string bag = (dir / "rows.cdx").string(), expr = "NAME";
            unsigned expected = 0;
            for (std::string tag : {std::string("FIRST"), std::string("SECOND")}) {
                // Stock rddads adsOrderCreate clears the order list first
                // when no additive order condition is active.
                REQUIRE(AdsCloseAllIndexes(t) == 0);
                ADSHANDLE idx = 0;
                if (legacy) REQUIRE(AdsCreateIndex(t, bc_bytes(bag), bc_bytes(tag), bc_bytes(expr), nullptr, ADS_COMPOUND, ADS_STRINGKEY, &idx) == 0);
                else REQUIRE(AdsCreateIndex61(t, bc_bytes(bag), bc_bytes(tag), bc_bytes(expr), nullptr, nullptr, ADS_COMPOUND, 512, &idx) == 0);
                ++expected;
                REQUIRE(AdsGetNumIndexes(t, &count) == 0);
                CHECK(count == expected);
                ADSHANDLE named = 0;
                REQUIRE(AdsGetIndexHandle(t, bc_bytes(tag), &named) == 0);
                std::string key = "00000005";
                UNSIGNED16 found = 0;
                REQUIRE(AdsSeek(named, bc_bytes(key), 8, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
                CHECK(found == 1);
                // RDD OrderCreate finishes with GoTop before OrdCount.
                REQUIRE(AdsGotoTop(idx) == 0);
                REQUIRE(AdsGetNumIndexes(t, &count) == 0);
                CHECK(count == expected);
                REQUIRE(AdsGetIndexHandle(t, bc_bytes(tag), &named) == 0);
                REQUIRE(AdsSeek(named, bc_bytes(key), 8, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
                CHECK(found == 1);
            }
            REQUIRE(AdsCloseTable(t) == 0);
            REQUIRE(AdsDisconnect(c) == 0);
        }
    }
}

TEST_CASE("scope settle: exact character scopes preserve C7 C8 C9 C10 C12 keys") {
    for (bool remote : {false, true}) {
        for (unsigned width : {7u,8u,9u,10u,12u}) {
            for (bool letters : {false, true}) {
                INFO("remote=" << remote << " width=" << width << " letters=" << letters);
                auto dir = bc_dir(std::string("scope_") + (remote ? "remote_" : "local_") + std::to_string(width) + (letters ? "_letters" : "_digits"));
                bc_seed(dir, width, letters, true);
                openads::network::Server srv;
                if (remote) REQUIRE(srv.start("127.0.0.1", 0));
                ADSHANDLE c = bc_connect(dir, remote ? srv.port() : 0), t = bc_open(c), idx = 0;
                std::string tag = "BYNAME", key = bc_key(width, 5, letters);
                REQUIRE(AdsGetIndexHandle(t, bc_bytes(tag), &idx) == 0);
                UNSIGNED16 found = 0, type = 0;
                REQUIRE(AdsGetKeyType(idx, &type) == 0);
                CHECK(type == ADS_STRING);
                REQUIRE(AdsSeek(idx, bc_bytes(key), static_cast<UNSIGNED16>(width), ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
                CHECK(found == 1);
                REQUIRE(AdsSetScope(idx, ADS_TOP, bc_bytes(key), static_cast<UNSIGNED16>(width), ADS_STRINGKEY) == 0);
                REQUIRE(AdsSetScope(idx, ADS_BOTTOM, bc_bytes(key), static_cast<UNSIGNED16>(width), ADS_STRINGKEY) == 0);
                REQUIRE(AdsGotoTop(idx) == 0);
                UNSIGNED16 eof = 1;
                REQUIRE(AdsAtEOF(t, &eof) == 0);
                CHECK(eof == 0);
                UNSIGNED32 count = 99;
                REQUIRE(AdsGetKeyCount(idx, ADS_RESPECTSCOPES, &count) == 0);
                CHECK(count == 2);
                REQUIRE(AdsCloseTable(t) == 0);
                REQUIRE(AdsDisconnect(c) == 0);
            }
        }
    }
}
