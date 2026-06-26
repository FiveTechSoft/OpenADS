// tests/unit/abi_aggregate_test.cpp
// Tier-3 server-side aggregation: AdsAggregate* ABI exports.
//
// AdsAggregate sends a FOR predicate + a compact aggregate spec
// ("COUNT:;SUM:QTY;AVG:QTY;MIN:NM;MAX:NM") to the server, which scans the
// whole table once, folds each matching row into the requested COUNT / SUM /
// AVG / MIN / MAX accumulators, and returns just the scalars — so a totalling
// report costs one round-trip instead of dragging every matched row over the
// wire.
//
// Test cases:
//   1. Local table -> AE_FUNCTION_NOT_AVAILABLE (caller falls back).
//   2. Remote wire: COUNT/SUM/AVG/MIN/MAX over a FOR predicate.
//   3. Remote wire: zero matches -> COUNT 0, SUM 0, AVG/MIN/MAX empty.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path agg_tmp_dir() {
    return fs::temp_directory_path() / "openads_agg_abi_test";
}

void agg_wipe() {
    std::error_code ec;
    fs::remove_all(agg_tmp_dir(), ec);
    fs::create_directories(agg_tmp_dir(), ec);
}

// Seed "agg.dbf" with NM (C,4) + QTY (N,8,2):
//   recno 1 -> NM="ANA", QTY=10.5
//   recno 2 -> NM="BIA", QTY=20
//   recno 3 -> NM="CAU", QTY=30
void seed_agg_fixture(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,4,0;QTY,N,8,2";
    UNSIGNED8 tname[] = "agg.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);

    UNSIGNED8 nm[]  = "NM";
    UNSIGNED8 qty[] = "QTY";
    auto add = [&](const char* name, double q) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, nm,
                  reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                  static_cast<UNSIGNED32>(std::strlen(name))) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hTable, qty, q) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    };
    add("ANA", 10.5);
    add("BIA", 20.0);
    add("CAU", 30.0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

std::string trim_right(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Open agg.dbf over a fresh in-process TCP server; returns conn+table handles.
struct RemoteFixture {
    openads::network::Server srv;
    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = 0;
    void open(const fs::path& dir) {
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        char uri[512];
        std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                      static_cast<unsigned>(srv.port()), dir.string().c_str());
        UNSIGNED8 srvbuf[512]{};
        std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
        REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER, nullptr, nullptr, 0,
                             &hConn) == AE_SUCCESS);
        UNSIGNED8 tname[] = "agg.dbf";
        REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                             ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
                == AE_SUCCESS);
    }
    ~RemoteFixture() {
        if (hTable) AdsCloseTable(hTable);
        if (hConn)  AdsDisconnect(hConn);
        srv.stop();
    }
};

// Read aggregate result i: returns its string value, sets `ty` to the type
// discriminator (0=empty, 1=numeric, 2=string).
std::string agg_value(ADSHANDLE hRes, UNSIGNED32 i, UNSIGNED16& ty) {
    UNSIGNED8 buf[64]{};
    UNSIGNED16 len = sizeof(buf);
    ty = 0;
    REQUIRE(AdsAggregateValue(hRes, i, &ty, buf, &len) == AE_SUCCESS);
    return std::string(reinterpret_cast<char*>(buf), len);
}

} // namespace

// ── 1. Local table — not applicable ──────────────────────────────────────────
TEST_CASE("AdsAggregate reports not-applicable on a local table") {
    agg_wipe();
    auto dir = agg_tmp_dir();

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "NM,C,4,0";
    UNSIGNED8 tname[] = "local_agg.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, def, &hTable) == AE_SUCCESS);

    UNSIGNED8 forc[] = "";
    UNSIGNED8 spec[] = "COUNT:";
    ADSHANDLE hRes   = 0;
    UNSIGNED32 rc = AdsAggregate(hTable, forc, spec, &hRes);
    CHECK(rc == AE_FUNCTION_NOT_AVAILABLE);
    CHECK(hRes == 0);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
}

// ── 2. Remote wire: full aggregate set over a FOR predicate ───────────────────
TEST_CASE("AdsAggregate remote wire: COUNT/SUM/AVG/MIN/MAX over a FOR predicate") {
    agg_wipe();
    auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);

    RemoteFixture fx;
    fx.open(dir);

    UNSIGNED8 forc[] = "QTY >= 20";
    UNSIGNED8 spec[] = "COUNT:;SUM:QTY;AVG:QTY;MIN:QTY;MAX:QTY;MIN:NM;MAX:NM";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsAggregate(fx.hTable, forc, spec, &hRes) == AE_SUCCESS);
    CHECK(hRes != 0);

    UNSIGNED32 n = 0;
    REQUIRE(AdsAggregateCount(hRes, &n) == AE_SUCCESS);
    CHECK(n == 7u);

    UNSIGNED16 ty = 0;
    CHECK(agg_value(hRes, 0, ty) == "2");   CHECK(ty == 1u);  // COUNT(*)
    CHECK(agg_value(hRes, 1, ty) == "50");  CHECK(ty == 1u);  // SUM QTY
    CHECK(agg_value(hRes, 2, ty) == "25");  CHECK(ty == 1u);  // AVG QTY
    CHECK(agg_value(hRes, 3, ty) == "20");  CHECK(ty == 1u);  // MIN QTY
    CHECK(agg_value(hRes, 4, ty) == "30");  CHECK(ty == 1u);  // MAX QTY
    CHECK(trim_right(agg_value(hRes, 5, ty)) == "BIA"); CHECK(ty == 2u);  // MIN NM
    CHECK(trim_right(agg_value(hRes, 6, ty)) == "CAU"); CHECK(ty == 2u);  // MAX NM

    REQUIRE(AdsAggregateClose(hRes) == AE_SUCCESS);
    UNSIGNED32 dummy = 0;
    CHECK(AdsAggregateCount(hRes, &dummy) != AE_SUCCESS);   // handle freed
}

// ── 3. Remote wire: zero matches ─────────────────────────────────────────────
TEST_CASE("AdsAggregate remote wire: zero matches -> 0 / 0 / empty") {
    agg_wipe();
    auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);

    RemoteFixture fx;
    fx.open(dir);

    UNSIGNED8 forc[] = "QTY > 1000";
    UNSIGNED8 spec[] = "COUNT:;SUM:QTY;AVG:QTY;MIN:NM";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsAggregate(fx.hTable, forc, spec, &hRes) == AE_SUCCESS);

    UNSIGNED16 ty = 0;
    CHECK(agg_value(hRes, 0, ty) == "0");  CHECK(ty == 1u);   // COUNT -> 0
    CHECK(agg_value(hRes, 1, ty) == "0");  CHECK(ty == 1u);   // SUM   -> 0
    CHECK(ty == 1u);
    agg_value(hRes, 2, ty);                CHECK(ty == 0u);   // AVG   -> empty
    agg_value(hRes, 3, ty);                CHECK(ty == 0u);   // MIN   -> empty

    REQUIRE(AdsAggregateClose(hRes) == AE_SUCCESS);
}
