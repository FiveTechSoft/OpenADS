// Tests for AdsGetKeyType — the index key expression type.
//
// AdsGetKeyType(hIndex, &usKeyType) must return the ACE *field-type*
// constants (ADS_STRING=4, ADS_NUMERIC=2, ADS_DATE=3, ADS_LOGICAL=1,
// ADS_RAW=16), NOT the AdsSetScope/AdsSeek buffer-encoding constants
// (ADS_STRINGKEY=1 / ADS_DOUBLEKEY=2 / ADS_RAWKEY=0).
//
// Regression for MLS2026: returning ADS_STRINGKEY (1) for a character
// key made Harbour's rddads read the answer as ADS_LOGICAL (1), so
// OrdScope() on any character-key tag took the logical branch and sent
// a 1-byte "T"/"F" scope instead of the real key value — scopes were
// silently destroyed for every character-key index.
#include "doctest.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

const char* remote_uri_env() { return std::getenv("OPENADS_TEST_REMOTE"); }

// Gregorian -> Julian Day Number (same Fliegel-Van Flandern formula as
// Harbour's hb_dateEncode and the engine's to_julian).
long jdn(int y, int m, int d) {
    long y32 = y, m32 = m, d32 = d;
    return (1461 * (y32 + 4800 + (m32 - 14) / 12)) / 4
         + (367  * (m32 - 2 - 12 * ((m32 - 14) / 12))) / 12
         - (3    * ((y32 + 4900 + (m32 - 14) / 12) / 100)) / 4
         + d32 - 32075;
}

} // namespace

TEST_CASE("AdsGetKeyType returns ACE field-type constants per key field") {
    auto dir = fs::temp_directory_path() / "openads_keytype";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "FSTR,C,6,0;FNUM,N,8,2;FDATE,D,8,0;FLOG,L,1,0";
    UNSIGNED8 tname[] = "kt";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 bag[] = "kt.cdx";

    struct Case { const char* tag; const char* expr; UNSIGNED16 want; };
    const Case cases[] = {
        { "TG_C", "FSTR",  ADS_STRING  },   // 4 — was wrongly 1 (=ADS_LOGICAL)
        { "TG_N", "FNUM",  ADS_NUMERIC },   // 2
        { "TG_D", "FDATE", ADS_DATE    },   // 3
        { "TG_L", "FLOG",  ADS_LOGICAL },   // 1
    };
    for (const auto& c : cases) {
        UNSIGNED8 tag[16];
        UNSIGNED8 expr[16];
        std::memcpy(tag,  c.tag,  std::strlen(c.tag)  + 1);
        std::memcpy(expr, c.expr, std::strlen(c.expr) + 1);
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                                 nullptr, nullptr, 0, 0, &hI) == 0);
        UNSIGNED16 kt = 0xFFFF;
        REQUIRE(AdsGetKeyType(hI, &kt) == 0);
        INFO("tag " << c.tag << " expr " << c.expr
             << " key type = " << kt << " want " << c.want);
        CHECK(kt == c.want);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsGetKeyType: computed expressions fall back by result type") {
    auto dir = fs::temp_directory_path() / "openads_keytype_expr";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "FSTR,C,6,0;FNUM,N,8,2";
    UNSIGNED8 tname[] = "kte";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 bag[] = "kte.cdx";

    // UPPER(char field) -> string result -> ADS_STRING
    {
        UNSIGNED8 tag[]  = "TG_UP";
        UNSIGNED8 expr[] = "UPPER(FSTR)";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                                 nullptr, nullptr, 0, 0, &hI) == 0);
        UNSIGNED16 kt = 0xFFFF;
        REQUIRE(AdsGetKeyType(hI, &kt) == 0);
        CHECK(kt == ADS_STRING);
    }
    // Val(char field) -> numeric result (FoxNumeric key) -> ADS_NUMERIC
    {
        UNSIGNED8 tag[]  = "TG_VAL";
        UNSIGNED8 expr[] = "Val(FSTR)";
        ADSHANDLE hI = 0;
        REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                                 nullptr, nullptr, 0, 0, &hI) == 0);
        UNSIGNED16 kt = 0xFFFF;
        REQUIRE(AdsGetKeyType(hI, &kt) == 0);
        CHECK(kt == ADS_NUMERIC);
    }

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetScope with ADS_DOUBLEKEY julian doubles scopes a date key") {
    // rddads sends OrdScope() date values as an 8-byte julian-day double
    // with ADS_DOUBLEKEY once AdsGetKeyType reports ADS_DATE. Our date
    // CDX keys are raw YYYYMMDD text, so AdsSetScope must convert the
    // julian double to the YYYYMMDD key form.
    auto dir = fs::temp_directory_path() / "openads_keytype_dscope";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "FDATE,D,8,0";
    UNSIGNED8 tname[] = "ktd";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 bag[]  = "ktd.cdx";
    UNSIGNED8 tag[]  = "TG_DT";
    UNSIGNED8 expr[] = "FDATE";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // 5 records: 2026-07-01 .. 2026-07-05
    UNSIGNED8 fld[] = "FDATE";
    for (int d = 1; d <= 5; ++d) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetJulian(hT, fld,
                             static_cast<SIGNED32>(jdn(2026, 7, d))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 5u);

    // Scope [2026-07-02 .. 2026-07-04] passed as julian doubles.
    double top = static_cast<double>(jdn(2026, 7, 2));
    double bot = static_cast<double>(jdn(2026, 7, 4));
    REQUIRE(AdsSetScope(hI, ADS_TOP,
                        reinterpret_cast<UNSIGNED8*>(&top),
                        sizeof(double), ADS_DOUBLEKEY) == 0);
    REQUIRE(AdsSetScope(hI, ADS_BOTTOM,
                        reinterpret_cast<UNSIGNED8*>(&bot),
                        sizeof(double), ADS_DOUBLEKEY) == 0);

    cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    INFO("date scope [20260702,20260704] key count = " << cnt);
    CHECK(cnt == 3u);

    REQUIRE(AdsClearScope(hI, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hI, ADS_BOTTOM) == 0);
    cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 5u);

    // DbSeek(date) path: rddads sends the same julian double with
    // ADS_DOUBLEKEY. Must find the 2026-07-03 record.
    double seek_jd = static_cast<double>(jdn(2026, 7, 3));
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(&seek_jd),
                    sizeof(double), ADS_DOUBLEKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    SIGNED32 got = 0;
    REQUIRE(AdsGetJulian(hT, fld, &got) == 0);
    CHECK(got == static_cast<SIGNED32>(jdn(2026, 7, 3)));

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(dir, ec);
}

// Seed helper for the remote scope test below. Gated on
// OPENADS_SEED_DIR: creates scope_ws.dbf + scope_ws.cdx (tag TG_RS on
// WRKORD, 7 rows 100001..100007) in that directory, ready to be copied
// into a server's data dir. Skipped in normal runs.
TEST_CASE("remote scope seed helper: build scope_ws table"
          * doctest::skip(std::getenv("OPENADS_SEED_DIR") == nullptr)) {
    const std::string dir = std::getenv("OPENADS_SEED_DIR");
    UNSIGNED8 srv[512] = {};
    REQUIRE(dir.size() < sizeof(srv));
    std::memcpy(srv, dir.c_str(), dir.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "WRKORD,C,6,0";
    UNSIGNED8 tname[] = "scope_ws";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 bag[]  = "scope_ws.cdx";
    UNSIGNED8 tag[]  = "TG_RS";
    UNSIGNED8 expr[] = "WRKORD";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    UNSIGNED8 fld[] = "WRKORD";
    for (int i = 1; i <= 7; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        char val[8];
        std::snprintf(val, sizeof(val), "10000%d", i);
        REQUIRE(AdsSetString(hT, fld,
                             reinterpret_cast<UNSIGNED8*>(val), 6) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
}

// Scope over the wire against a live openads_serverd whose data dir
// holds the scope_ws table built by the seed helper above. Gated on
// OPENADS_TEST_REMOTE (server URI, e.g.
// "tcp://192.168.18.184:16262//tmp/openads_mac"), skipped otherwise.
// Exercises the rddads OrdScope flow end-to-end on a remote table:
// character key, digit-only values, full-string ADS_STRINGKEY scope.
TEST_CASE("remote scope over the wire: character key + key count + walk"
          * doctest::skip(remote_uri_env() == nullptr)) {
    const std::string uri = remote_uri_env();
    UNSIGNED8 srv[512] = {};
    REQUIRE(uri.size() < sizeof(srv));
    std::memcpy(srv, uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "scope_ws.dbf";
    UNSIGNED8 alias[] = "scpws";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);

    // Production CDX tag, resolved the way rddads' SET ORDER does;
    // navigation then goes through the index handle (hOrdCurrent).
    ADSHANDLE hI = 0;
    UNSIGNED8 tag[] = "TG_RS";
    REQUIRE(AdsGetIndexHandle(hT, tag, &hI) == 0);

    UNSIGNED8 fld[] = "WRKORD";
    auto walk_count = [&]() -> int {
        if (AdsGotoTop(hI) != 0) return -1;
        int n = 0;
        for (;;) {
            // rddads checks EOF on the TABLE handle while navigating
            // through the index handle (hOrdCurrent).
            UNSIGNED16 at_eof = 0;
            if (AdsAtEOF(hT, &at_eof) != 0) return -1;
            if (at_eof) break;
            ++n;
            if (n > 32) return -2;      // runaway guard
            if (AdsSkip(hI, 1) != 0) return -1;
        }
        return n;
    };

    // Full walk without scope.
    CHECK(walk_count() == 7);

    // rddads OrdScope flow: full-string ADS_STRINGKEY scope.
    UNSIGNED8 top_key[]    = "100003";
    UNSIGNED8 bottom_key[] = "100005";
    REQUIRE(AdsSetScope(hI, ADS_TOP, top_key, 6, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hI, ADS_BOTTOM, bottom_key, 6, ADS_STRINGKEY) == 0);

    CHECK(walk_count() == 3);

    // First in-scope record is the TOP bound.
    REQUIRE(AdsGotoTop(hI) == 0);
    UNSIGNED8  buf[16] = {};
    UNSIGNED32 blen    = sizeof(buf);
    REQUIRE(AdsGetString(hT, fld, buf, &blen, 0) == 0);
    CHECK(std::memcmp(buf, "100003", 6) == 0);

    // Scoped key count over the wire.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    INFO("remote scoped key count = " << cnt);
    CHECK(cnt == 3u);

    // Clear scope: everything visible again.
    REQUIRE(AdsClearScope(hI, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hI, ADS_BOTTOM) == 0);
    CHECK(walk_count() == 7);
    cnt = 0;
    REQUIRE(AdsGetKeyCount(hI, 0, &cnt) == 0);
    CHECK(cnt == 7u);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
}

// M12.35 — remote AdsGetKeyType + date scope/seek over the wire.
// Creates a CDX table with a DATE field, seeds 5 rows, starts an
// in-process server, connects remotely, and verifies:
//   1. AdsGetKeyType returns ADS_DATE (not ADS_STRING) for remote index
//   2. AdsSetScope with ADS_DOUBLEKEY julian doubles works over remote
//   3. AdsSeek with ADS_DOUBLEKEY julian double works over remote

#include "network/server.h"

namespace {

ADSHANDLE remote_connect_keytype(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

} // namespace

TEST_CASE("M12.35 remote AdsGetKeyType returns ADS_DATE for DATE index") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_keytype_date";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Seed: local connection creates the table + index + data.
    UNSIGNED8 lsrv[256];
    std::memcpy(lsrv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(lsrv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[]   = "FDATE,D,8,0";
    UNSIGNED8 tname[] = "rdate";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    UNSIGNED8 bag[]  = "rdate.cdx";
    UNSIGNED8 tag[]  = "TG_DT";
    UNSIGNED8 expr[] = "FDATE";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // 5 records: 2026-07-01 .. 2026-07-05
    UNSIGNED8 fld[] = "FDATE";
    for (int d = 1; d <= 5; ++d) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetJulian(hT, fld,
                             static_cast<SIGNED32>(jdn(2026, 7, d))) == 0);
    }
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // Start in-process server + connect remotely.
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect_keytype(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    ADSHANDLE hOrd = 0;
    UNSIGNED8 want_tag[] = "TG_DT";
    REQUIRE(AdsGetIndexHandle(hRT, want_tag, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    SUBCASE("AdsGetKeyType returns ADS_DATE for remote DATE index") {
        UNSIGNED16 kt = 0xFFFF;
        REQUIRE(AdsGetKeyType(hOrd, &kt) == 0);
        INFO("remote date key type = " << kt << " want " << ADS_DATE);
        CHECK(kt == ADS_DATE);
    }

    SUBCASE("AdsSetScope with ADS_DOUBLEKEY scopes remote date index") {
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetKeyCount(hOrd, 0, &cnt) == 0);
        CHECK(cnt == 5u);

        double top = static_cast<double>(jdn(2026, 7, 2));
        double bot = static_cast<double>(jdn(2026, 7, 4));
        REQUIRE(AdsSetScope(hOrd, ADS_TOP,
                            reinterpret_cast<UNSIGNED8*>(&top),
                            sizeof(double), ADS_DOUBLEKEY) == 0);
        REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM,
                            reinterpret_cast<UNSIGNED8*>(&bot),
                            sizeof(double), ADS_DOUBLEKEY) == 0);

        cnt = 0;
        REQUIRE(AdsGetKeyCount(hOrd, 0, &cnt) == 0);
        INFO("remote date scope [0702,0704] key count = " << cnt);
        CHECK(cnt == 3u);

        // Walk: first in-scope should be 2026-07-02.
        REQUIRE(AdsGotoTop(hOrd) == 0);
        SIGNED32 got = 0;
        REQUIRE(AdsGetJulian(hRT, fld, &got) == 0);
        CHECK(got == static_cast<SIGNED32>(jdn(2026, 7, 2)));

        REQUIRE(AdsClearScope(hOrd, ADS_TOP) == 0);
        REQUIRE(AdsClearScope(hOrd, ADS_BOTTOM) == 0);
        cnt = 0;
        REQUIRE(AdsGetKeyCount(hOrd, 0, &cnt) == 0);
        CHECK(cnt == 5u);
    }

    SUBCASE("AdsSeek with ADS_DOUBLEKEY finds record over remote") {
        double seek_jd = static_cast<double>(jdn(2026, 7, 3));
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hOrd, reinterpret_cast<UNSIGNED8*>(&seek_jd),
                        sizeof(double), ADS_DOUBLEKEY,
                        ADS_HARDSEEK, &found) == 0);
        CHECK(found == 1);
        SIGNED32 got = 0;
        REQUIRE(AdsGetJulian(hRT, fld, &got) == 0);
        CHECK(got == static_cast<SIGNED32>(jdn(2026, 7, 3)));
    }

    AdsCloseTable(hRT);
    AdsDisconnect(hRC);
}
