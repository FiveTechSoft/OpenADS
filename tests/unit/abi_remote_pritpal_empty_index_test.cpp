// Remote twin of abi_pritpal_empty_index_test.cpp — Pritpal Bedi
// StageStressTest_CDX over tcp:// (ADS_REMOTE_SERVER).
//
// Critical question: after INDEX ON empty + close + remote USE + append
// without explicit SET INDEX, does the production CDX get keys (local
// does via auto-open) or stay empty (his TestIndex.zip: 36 rows / 0 keys)?
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    REQUIRE(hConn != 0);
    return hConn;
}

ADSHANDLE remote_create_empty(ADSHANDLE hConn, const char* name) {
    UNSIGNED8 flds[] = "NAME,C,20,0;CITY,C,20,0";
    std::vector<UNSIGNED8> n(name, name + std::strlen(name) + 1);
    ADSHANDLE hTbl = 0;
    REQUIRE(AdsCreateTable(hConn, n.data(), n.data(), ADS_CDX, ADS_ANSI,
                           0, 0, 0, flds, &hTbl) == 0);
    REQUIRE(hTbl != 0);
    return hTbl;
}

void append_name(ADSHANDLE hTbl, const char* name, const char* city) {
    REQUIRE(AdsAppendRecord(hTbl) == 0);
    REQUIRE(AdsSetString(hTbl,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                         static_cast<UNSIGNED32>(std::strlen(name))) == 0);
    REQUIRE(AdsSetString(hTbl,
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>("CITY")),
                         reinterpret_cast<UNSIGNED8*>(const_cast<char*>(city)),
                         static_cast<UNSIGNED32>(std::strlen(city))) == 0);
    REQUIRE(AdsWriteRecord(hTbl) == 0);
}

std::pair<UNSIGNED32, std::string> try_get_name(ADSHANDLE hTbl) {
    UNSIGNED8 buf[64] = {};
    UNSIGNED32 len = sizeof(buf);
    UNSIGNED32 rc = AdsGetString(
        hTbl, reinterpret_cast<UNSIGNED8*>(const_cast<char*>("NAME")),
        buf, &len, ADS_NONE);
    std::string s(reinterpret_cast<char*>(buf));
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return {rc, s};
}

std::string get_name(ADSHANDLE hTbl) {
    auto [rc, s] = try_get_name(hTbl);
    REQUIRE(rc == 0);
    return s;
}

bool at_bof(ADSHANDLE h) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtBOF(h, &v) == 0);
    return v != 0;
}

bool at_eof(ADSHANDLE h) {
    UNSIGNED16 v = 0;
    REQUIRE(AdsAtEOF(h, &v) == 0);
    return v != 0;
}

UNSIGNED32 key_count(ADSHANDLE hIdx) {
    UNSIGNED32 n = 0xFFFFFFFFu;
    REQUIRE(AdsGetKeyCount(hIdx, ADS_IGNOREFILTERS, &n) == 0);
    return n;
}

UNSIGNED32 rec_count(ADSHANDLE hTbl) {
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hTbl, ADS_IGNOREFILTERS, &n) == 0);
    return n;
}

// Server is non-copyable — hold it by unique_ptr so the env can be moved.
struct RemoteEnv {
    std::unique_ptr<openads::network::Server> srv;
    fs::path data;
    ADSHANDLE hConn = 0;

    static RemoteEnv start(const char* leaf) {
        RemoteEnv e;
        e.data = fs::temp_directory_path() / leaf;
        std::error_code ec;
        fs::remove_all(e.data, ec);
        fs::create_directories(e.data);
        e.srv = std::make_unique<openads::network::Server>();
        REQUIRE(e.srv->start("127.0.0.1", 0).has_value());
        e.hConn = remote_connect(e.data, e.srv->port());
        return e;
    }

    void stop() {
        if (hConn) {
            AdsDisconnect(hConn);
            hConn = 0;
        }
        if (srv) srv->stop();
        std::error_code ec;
        fs::remove_all(data, ec);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// A) Remote INDEX ON empty → keycount 0; GoTop OK; FieldGet 5068
// ---------------------------------------------------------------------------
TEST_CASE("remote Pritpal empty-index: INDEX ON empty yields keycount 0") {
    auto env = RemoteEnv::start("openads_r_pritpal_empty_a");
    auto hTbl = remote_create_empty(env.hConn, "TestIndex");

    UNSIGNED8 bag[]  = "TestIndex.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 0u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(at_bof(hTbl));
    CHECK(at_eof(hTbl));
    auto [rc, name] = try_get_name(hTbl);
    // Local returns 5068; remote may return 0 with a blank value. Either
    // means "not on a record" — rddads shows blank either way.
    CHECK((rc == AE_NO_CURRENT_RECORD || (rc == 0 && name.empty())));
    CHECK(name.empty());

    REQUIRE(AdsCloseTable(hTbl) == 0);
    env.stop();
}

// ---------------------------------------------------------------------------
// B) Non-structural bag: never opened during append → stale on disk; the
//    server's AdsOpenIndex now heals it (reindex once on open), so the
//    client gets a working order instead of the ghost bof=eof=1 Limbo.
// ---------------------------------------------------------------------------
TEST_CASE("remote Pritpal empty-index: stale non-structural bag is healed on open") {
    auto env = RemoteEnv::start("openads_r_pritpal_empty_b");
    auto hTbl = remote_create_empty(env.hConn, "TestIndex");

    UNSIGNED8 bag[]  = "MyIdx.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    hTbl = 0;

    UNSIGNED8 tname[] = "TestIndex";
    REQUIRE(AdsOpenTable(env.hConn, tname, tname, ADS_CDX, 1, 1, 0, 1,
                         &hTbl) == 0);
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    append_name(hTbl, "Charlie", "Valencia");
    CHECK(rec_count(hTbl) == 3u);

    REQUIRE(AdsGotoTop(hTbl) == 0);
    CHECK(get_name(hTbl) == "Alice");

    // Open the stale bag over the wire: the server-side twin reindexes it
    // once, on open.
    std::array<ADSHANDLE, 8> arr{};
    UNSIGNED16 n = static_cast<UNSIGNED16>(arr.size());
    REQUIRE(AdsOpenIndex(hTbl, bag, arr.data(), &n) == 0);
    REQUIRE(n >= 1u);
    CHECK(key_count(arr[0]) == 3u);
    REQUIRE(AdsGotoTop(arr[0]) == 0);
    CHECK_FALSE(at_bof(hTbl));
    CHECK_FALSE(at_eof(hTbl));
    CHECK(get_name(hTbl) == "Alice");

    // Rebuild after data over the wire.
    REQUIRE(AdsCloseTable(hTbl) == 0);
    // Delete bag server-side via local filesystem under data dir.
    std::error_code ec;
    fs::remove(env.data / "MyIdx.cdx", ec);
    REQUIRE(AdsOpenTable(env.hConn, tname, tname, ADS_CDX, 1, 1, 0, 1,
                         &hTbl) == 0);
    hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 3u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    env.stop();
}

// ---------------------------------------------------------------------------
// C) Production bag auto-open + remote append must maintain keys (parity
//    with local). Failure here would explain Pritpal's 36-row / 0-key bag
//    under structural TestIndex.cdx on remote.
// ---------------------------------------------------------------------------
TEST_CASE("remote Pritpal empty-index: production CDX auto-open maintained on append") {
    auto env = RemoteEnv::start("openads_r_pritpal_empty_c");
    auto hTbl = remote_create_empty(env.hConn, "TestIndex");

    UNSIGNED8 bag[]  = "TestIndex.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 0u);
    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTbl) == 0);
    hTbl = 0;

    UNSIGNED8 tname[] = "TestIndex";
    REQUIRE(AdsOpenTable(env.hConn, tname, tname, ADS_CDX, 1, 1, 0, 1,
                         &hTbl) == 0);
    // Production auto-open only — no explicit AdsOpenIndex before write.
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    append_name(hTbl, "Charlie", "Valencia");
    CHECK(rec_count(hTbl) == 3u);

    std::array<ADSHANDLE, 8> arr{};
    UNSIGNED16 n = static_cast<UNSIGNED16>(arr.size());
    REQUIRE(AdsOpenIndex(hTbl, bag, arr.data(), &n) == 0);
    REQUIRE(n >= 1u);
    // Expect parity with local production-bag maintenance. If this is 0
    // while the table has 3 rows, remote append is not syncing the
    // auto-opened production bag — that is an OpenADS gap (matches a
    // 36-row / 0-key TestIndex.cdx under remote staging).
    const UNSIGNED32 kc = key_count(arr[0]);
    CHECK(kc == 3u);
    if (kc == 3u) {
        REQUIRE(AdsGotoTop(arr[0]) == 0);
        CHECK(get_name(hTbl) == "Alice");
    }

    REQUIRE(AdsCloseTable(hTbl) == 0);
    env.stop();
}

// ---------------------------------------------------------------------------
// D) INDEX ON after data over the wire
// ---------------------------------------------------------------------------
TEST_CASE("remote Pritpal empty-index: INDEX ON after append populates keys") {
    auto env = RemoteEnv::start("openads_r_pritpal_empty_d");
    auto hTbl = remote_create_empty(env.hConn, "TestIndex");
    append_name(hTbl, "Charlie", "Valencia");
    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");

    UNSIGNED8 bag[]  = "TestIndex.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 3u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    env.stop();
}

// ---------------------------------------------------------------------------
// E) Append with order open grows keycount over the wire
// ---------------------------------------------------------------------------
TEST_CASE("remote Pritpal empty-index: append with order open grows keycount") {
    auto env = RemoteEnv::start("openads_r_pritpal_empty_e");
    auto hTbl = remote_create_empty(env.hConn, "TestIndex");

    UNSIGNED8 bag[]  = "MyIdx.cdx";
    UNSIGNED8 tag[]  = "IDX01";
    UNSIGNED8 expr[] = "NAME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTbl, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hIdx) == 0);
    CHECK(key_count(hIdx) == 0u);

    append_name(hTbl, "Alice", "Madrid");
    append_name(hTbl, "Bob", "Barcelona");
    CHECK(key_count(hIdx) == 2u);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(get_name(hTbl) == "Alice");

    REQUIRE(AdsCloseTable(hTbl) == 0);
    env.stop();
}
