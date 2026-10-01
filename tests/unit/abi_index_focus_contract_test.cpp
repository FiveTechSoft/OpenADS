// Harbour rddads focus contract. Bug report relayed by Pritpal Bedi.
// rddads uppercases name lookup; focus 0/"" only changes its hOrdCurrent.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include <filesystem>
#include <string>
#include <vector>
#include <cstdlib>
namespace {
UNSIGNED8* focus_bytes(std::string& s) { return reinterpret_cast<UNSIGNED8*>(s.data()); }
void focus_expect_rec(ADSHANDLE t, UNSIGNED32 n) {
    UNSIGNED32 got = 0;
    REQUIRE(AdsGetRecordNum(t, ADS_IGNOREFILTERS, &got) == 0);
    INFO("expected rec=" << n << " table=" << t);
    CHECK(got == n);
}
void focus_fixture(const std::filesystem::path& dir, UNSIGNED16 type) {
    std::filesystem::create_directories(dir);
    std::string path = dir.string(); ADSHANDLE c = 0, t = 0;
    REQUIRE(AdsConnect60(focus_bytes(path), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &c) == 0);
    std::string name = type == ADS_ADT ? "focus.adt" : "focus.dbf";
    std::string defs = "lower,Character,8;UPPER,Character,8";
    REQUIRE(AdsCreateTable(c, focus_bytes(name), nullptr, type, ADS_ANSI,
        ADS_PROPRIETARY_LOCKING, 0, 0, focus_bytes(defs), &t) == 0);
    for (const char* value : {"D", "B", "A", "C"}) {
        REQUIRE(AdsAppendRecord(t) == 0);
        std::string k = "lower", k2 = "UPPER", v = value;
        REQUIRE(AdsSetString(t, focus_bytes(k), focus_bytes(v), 1) == 0);
        REQUIRE(AdsSetString(t, focus_bytes(k2), focus_bytes(v), 1) == 0);
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    std::string bag = (dir / (type == ADS_ADT ? "focus.adi" : "focus.cdx")).string();
    for (const char* tag : {"lower", "UPPER"}) {
        std::string tagname = tag, expr = tag; ADSHANDLE h = 0;
        REQUIRE(AdsCreateIndex61(t, focus_bytes(bag), focus_bytes(tagname),
            focus_bytes(expr), nullptr, nullptr, ADS_COMPOUND, 512, &h) == 0);
    }
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(c) == 0);
}
void focus_check(const std::filesystem::path& dir, UNSIGNED16 type, bool remote) {
    INFO("type=" << type << " remote=" << remote);
    openads::network::Server srv;
    std::string path = dir.string();
    if (remote) {
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        path = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;
    }
    ADSHANDLE c = 0, t = 0;
    REQUIRE(AdsConnect60(focus_bytes(path), remote ? ADS_REMOTE_SERVER : ADS_LOCAL_SERVER,
        nullptr, nullptr, 0, &c) == 0);
    std::string name = type == ADS_ADT ? "focus.adt" : "focus.dbf";
    REQUIRE(AdsOpenTable(c, focus_bytes(name), nullptr, type, ADS_ANSI,
        ADS_PROPRIETARY_LOCKING, 0, ADS_SHARED, &t) == 0);
    REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 1);
    ADSHANDLE first = 0, second = 0;
    REQUIRE(AdsGetIndexHandleByOrder(t, 1, &first) == 0);
    REQUIRE(AdsGetIndexHandleByOrder(t, 2, &second) == 0);
    for (const char* tag : {"lower", "LOWER", "LoWeR  ", "upper", "UPPER"}) {
        std::string wanted = tag; ADSHANDLE h = 0;
        REQUIRE(AdsGetIndexHandle(t, focus_bytes(wanted), &h) == 0);
        CHECK(h == (wanted[0] == 'u' || wanted[0] == 'U' ? second : first));
    }
    // Exactly stock rddads' zero and empty-name sequence: no reset API call.
    for (int repeat = 0; repeat < 2; ++repeat) {
        { INFO("index top second"); REQUIRE(AdsGotoTop(second) == 0); focus_expect_rec(t, 3); }
        REQUIRE(AdsSkip(second, 1) == 0); focus_expect_rec(t, 2);
        REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 1);
        REQUIRE(AdsSkip(t, 1) == 0); focus_expect_rec(t, 2);
        REQUIRE(AdsGotoBottom(t) == 0); focus_expect_rec(t, 4);
        { INFO("natural backwards skip"); REQUIRE(AdsSkip(t, -1) == 0); focus_expect_rec(t, 3); }
        // Direct Skip must restore natural order without a preceding GoTop.
        { INFO("index top first"); REQUIRE(AdsGotoTop(first) == 0); focus_expect_rec(t, 3); }
        { INFO("direct natural skip"); REQUIRE(AdsSkip(t, 1) == 0); focus_expect_rec(t, 4); }
    }
    // Retained scopes must not keep natural focus indexed.
    std::string scope = "B";
    REQUIRE(AdsSetScope(first, ADS_TOP, focus_bytes(scope), 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsGotoTop(first) == 0); focus_expect_rec(t, 2);
    REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 1);
    REQUIRE(AdsGotoTop(first) == 0); focus_expect_rec(t, 2);
    REQUIRE(AdsClearScope(first, ADS_TOP) == 0);
    // Explicit focus APIs must still opt table-handle calls into index order.
    REQUIRE(AdsSetIndexOrderByHandle(t, second) == 0);
    REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 3);
    REQUIRE(AdsSkip(t, 1) == 0); focus_expect_rec(t, 2);
    for (int repeat = 0; repeat < 2; ++repeat) {
        REQUIRE(AdsSetIndexOrderByHandle(t, 0) == 0);
        REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 1);
    }
    std::string tag = "LOWER", empty;
    REQUIRE(AdsSetIndexOrder(t, focus_bytes(tag)) == 0);
    REQUIRE(AdsGotoBottom(t) == 0); focus_expect_rec(t, 1);
    REQUIRE(AdsSetIndexOrder(t, focus_bytes(empty)) == 0);
    REQUIRE(AdsGotoBottom(t) == 0); focus_expect_rec(t, 4);
    // A newly created tag supports historical table-handle navigation,
    // then stock rddads index navigation makes table calls natural again.
    std::string extra = "EXTRA", expr = "lower"; ADSHANDLE fresh = 0;
    std::string bag = (dir / (type == ADS_ADT ? "new.adi" : "new.cdx")).string();
    REQUIRE(AdsCreateIndex61(t, focus_bytes(bag), focus_bytes(extra),
        focus_bytes(expr), nullptr, nullptr, ADS_COMPOUND, 512, &fresh) == 0);
    REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 3);
    REQUIRE(AdsGotoTop(fresh) == 0); focus_expect_rec(t, 3);
    REQUIRE(AdsGotoTop(t) == 0); focus_expect_rec(t, 1);
    // Natural focus must leave every tag open, and all keys maintained.
    UNSIGNED16 count = 0; REQUIRE(AdsGetNumIndexes(t, &count) == 0); CHECK(count == 3);
    REQUIRE(AdsAppendRecord(t) == 0);
    std::string key = "lower", key2 = "UPPER", v = "0";
    REQUIRE(AdsSetString(t, focus_bytes(key), focus_bytes(v), 1) == 0);
    REQUIRE(AdsSetString(t, focus_bytes(key2), focus_bytes(v), 1) == 0);
    REQUIRE(AdsWriteRecord(t) == 0);
    REQUIRE(AdsGotoTop(first) == 0); focus_expect_rec(t, 5);
    REQUIRE(AdsGotoTop(second) == 0); focus_expect_rec(t, 5);
    REQUIRE(AdsCloseTable(t) == 0); REQUIRE(AdsDisconnect(c) == 0);
    if (remote) srv.stop();
}
}
TEST_CASE("RDD focus: case-insensitive tags and implicit index to natural navigation") {
    // Default native ADI and DBF/CDX. Rerouted ADI is exercised by the
    // companion invocation with OPENADS_ADT_CDX_INDEX=1 (cached configuration).
    for (UNSIGNED16 type : {static_cast<UNSIGNED16>(ADS_ADT), static_cast<UNSIGNED16>(ADS_CDX)}) {
        for (bool remote : {false, true}) {
            auto dir = std::filesystem::temp_directory_path() /
                ("openads_focus_contract_" + std::to_string(type) + (remote ? "_remote" : "_local"));
            std::error_code ec; std::filesystem::remove_all(dir, ec);
            focus_fixture(dir, type); focus_check(dir, type, remote);
            std::filesystem::remove_all(dir, ec);
        }
    }
}
