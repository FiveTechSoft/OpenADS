// AdsExtractKey on a REMOTE index (Harbour rddads OrdKeyVal).
// Field report: "ordkeyval() returns garbage". The remote path did not
// exist: AdsExtractKey failed for a TCP index handle, rddads ignored the
// return code and decoded its unfilled key buffer at full length.
// Checks that every tag's remote key equals the local engine's key for
// the same record (forward and backward walks, so the client's read-ahead
// leaves the server cursor behind), that a written value shows up in the
// key, and that failures report an empty key.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_to(const std::string& where, UNSIGNED16 type) {
    std::vector<UNSIGNED8> buf(where.begin(), where.end());
    buf.push_back(0);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(buf.data(), type, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE open_table(ADSHANDLE conn) {
    UNSIGNED8 name[] = "ekey.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    return t;
}

void make_table(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    ADSHANDLE conn = connect_to(dir.string(), ADS_LOCAL_SERVER);
    UNSIGNED8 fields[] = "NAME,Character,12;AMT,Numeric,10,2;WHEN,Date,8";
    UNSIGNED8 tname[]  = "ekey.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsCreateTable(conn, tname, nullptr, ADS_CDX, 0, 0, 0, 64,
                           fields, &t) == 0);
    for (int i = 0; i < 120; ++i) {
        REQUIRE(AdsAppendRecord(t) == 0);
        // Out-of-order values so every tag walks in a different order.
        const std::string name = "N" + std::to_string((i * 37) % 120);
        UNSIGNED8 fN[] = "NAME", fA[] = "AMT", fW[] = "WHEN";
        AdsSetString(t, fN, (UNSIGNED8*)name.c_str(),
                     static_cast<UNSIGNED32>(name.size()));
        AdsSetDouble(t, fA, ((i * 53) % 120) * 1.25 - 40.0);
        char d[16];
        std::snprintf(d, sizeof d, "%04d%02d%02d", 2000 + (i * 7) % 25,
                      1 + i % 12, 1 + i % 28);
        AdsSetDate(t, fW, (UNSIGNED8*)d, 8);
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    struct Tag { const char* tag; const char* expr; };
    const Tag tags[] = {{"TNAME", "NAME"}, {"TAMT", "AMT"},
                        {"TWHEN", "WHEN"},
                        {"TMIX", "NAME+DTOS(WHEN)+STR(AMT,10,2)"}};
    for (const auto& tg : tags) {
        ADSHANDLE hi = 0;
        UNSIGNED8 bag[] = "ekey.cdx";
        REQUIRE(AdsCreateIndex61(t, bag, (UNSIGNED8*)tg.tag,
                                 (UNSIGNED8*)tg.expr, nullptr, nullptr,
                                 ADS_COMPOUND, 512, &hi) == 0);
    }
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

std::string key_of(ADSHANDLE hi, UNSIGNED32* rc_out = nullptr) {
    UNSIGNED8 buf[300];
    std::memset(buf, 0xCC, sizeof buf);
    UNSIGNED16 len = 256;
    UNSIGNED32 rc = AdsExtractKey(hi, buf, &len);
    if (rc_out) *rc_out = rc;
    REQUIRE(len <= 256);
    return std::string(reinterpret_cast<char*>(buf), len);
}

} // namespace

TEST_CASE("remote AdsExtractKey returns the same key as the local engine") {
    const auto dir = fs::temp_directory_path() / "openads_remote_extract_key";
    make_table(dir);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) +
                            "/" + dir.generic_string();

    ADSHANDLE cl = connect_to(dir.string(), ADS_LOCAL_SERVER);
    ADSHANDLE cr = connect_to(uri, ADS_REMOTE_SERVER);
    ADSHANDLE tl = open_table(cl), tr = open_table(cr);

    UNSIGNED16 n = 0;
    REQUIRE(AdsGetNumIndexes(tr, &n) == 0);
    REQUIRE(n == 4);
    std::vector<ADSHANDLE> hl(n), hr(n);
    for (UNSIGNED16 i = 0; i < n; ++i) {
        REQUIRE(AdsGetIndexHandleByOrder(tl, i + 1, &hl[i]) == 0);
        REQUIRE(AdsGetIndexHandleByOrder(tr, i + 1, &hr[i]) == 0);
    }

    SUBCASE("walk forward then back by each tag, compare every tag") {
        for (UNSIGNED16 drive = 0; drive < n; ++drive) {
            REQUIRE(AdsGotoTop(hr[drive]) == 0);
            int rows = 0;
            for (int step = 0; step < 160; ++step) {
                UNSIGNED16 eof = 0, bof = 0;
                AdsAtEOF(tr, &eof);
                AdsAtBOF(tr, &bof);
                if (eof || bof) break;
                UNSIGNED32 rec = 0;
                REQUIRE(AdsGetRecordNum(tr, ADS_IGNOREFILTERS, &rec) == 0);
                REQUIRE(AdsGotoRecord(tl, rec) == 0);
                for (UNSIGNED16 i = 0; i < n; ++i) {
                    UNSIGNED32 rc = 0;
                    const std::string remote = key_of(hr[i], &rc);
                    CHECK(rc == 0);
                    const std::string local = key_of(hl[i]);
                    INFO("drive tag " << drive + 1 << ", tag " << i + 1
                         << ", rec " << rec);
                    CHECK(remote == local);
                    CHECK(!remote.empty());
                }
                ++rows;
                REQUIRE(AdsSkip(hr[drive], step < 100 ? 1 : -1) == 0);
            }
            CHECK(rows == 160);
        }
    }

    SUBCASE("a written value shows up in the remote key") {
        REQUIRE(AdsGotoTop(hr[0]) == 0);
        REQUIRE(AdsLockRecord(tr, 0) == 0);
        UNSIGNED8 fN[] = "NAME";
        UNSIGNED8 v[] = "ZZTOP";
        REQUIRE(AdsSetString(tr, fN, v, 5) == 0);
        REQUIRE(AdsWriteRecord(tr) == 0);
        const std::string k = key_of(hr[0]);
        CHECK(k.substr(0, 5) == "ZZTOP");
        REQUIRE(AdsUnlockRecord(tr, 0) == 0);
    }

    SUBCASE("a failing call reports an empty key, not the caller's length") {
        UNSIGNED8 buf[64];
        UNSIGNED16 len = sizeof buf;
        CHECK(AdsExtractKey(0xDEADBEEF, buf, &len) != 0);
        CHECK(len == 0);
    }

    AdsCloseTable(tl);
    AdsCloseTable(tr);
    AdsDisconnect(cr);
    AdsDisconnect(cl);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
