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
//   1. Local table: COUNT/SUM/AVG/MIN/MAX over a FOR predicate.
//   2. Remote wire: COUNT/SUM/AVG/MIN/MAX over a FOR predicate.
//   3. Remote wire: zero matches -> COUNT 0, SUM 0, AVG/MIN/MAX empty.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// ── 1. Local table: full aggregate set over a FOR predicate ─────────────────
TEST_CASE("AdsAggregate local: COUNT/SUM/AVG/MIN/MAX over a FOR predicate") {
    agg_wipe();
    auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 tname[] = "agg.dbf";
    ADSHANDLE hTable  = 0;
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, ADS_ANSI, ADS_SHARED,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hTable)
            == AE_SUCCESS);

    UNSIGNED8 forc[] = "QTY >= 20";
    UNSIGNED8 spec[] = "COUNT:;SUM:QTY;AVG:QTY;MIN:QTY;MAX:QTY;MIN:NM;MAX:NM";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsAggregate(hTable, forc, spec, &hRes) == AE_SUCCESS);

    UNSIGNED32 n = 0;
    REQUIRE(AdsAggregateCount(hRes, &n) == AE_SUCCESS);
    CHECK(n == 7u);

    UNSIGNED16 ty = 0;
    CHECK(agg_value(hRes, 0, ty) == "2");   CHECK(ty == 1u);
    CHECK(agg_value(hRes, 1, ty) == "50");  CHECK(ty == 1u);
    CHECK(agg_value(hRes, 2, ty) == "25");  CHECK(ty == 1u);
    CHECK(agg_value(hRes, 3, ty) == "20");  CHECK(ty == 1u);
    CHECK(agg_value(hRes, 4, ty) == "30");  CHECK(ty == 1u);
    CHECK(trim_right(agg_value(hRes, 5, ty)) == "BIA"); CHECK(ty == 2u);
    CHECK(trim_right(agg_value(hRes, 6, ty)) == "CAU"); CHECK(ty == 2u);

    REQUIRE(AdsAggregateClose(hRes) == AE_SUCCESS);
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

// ── 4. Remote wire: an unknown field must be rejected, not silently counted ──
// Regression for the Tier-3 review (#113): the server resolved each spec's
// field with field_index(), which returns -1 for a name that isn't in the
// table -- and the scan loop treated fidx < 0 as COUNT(*). So `SUM:NOPE`
// silently returned the *row count* as a numeric total with AE_SUCCESS,
// masking a typo'd or injected field name. It must fail instead.
TEST_CASE("AdsAggregate remote wire: unknown field is rejected, not silent COUNT") {
    agg_wipe();
    auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);

    RemoteFixture fx;
    fx.open(dir);

    UNSIGNED8 forc[] = "";
    UNSIGNED8 spec[] = "SUM:NOPE";        // NOPE is not a column of agg.dbf
    ADSHANDLE hRes   = 0;
    CHECK(AdsAggregate(fx.hTable, forc, spec, &hRes) != AE_SUCCESS);
    CHECK(hRes == 0);
}

// ── 5. Remote wire: an empty field is only valid for COUNT ───────────────────
// `SUM:` / `AVG:` / `MIN:` / `MAX:` with no field used to fall through the same
// fidx < 0 path and be folded as COUNT(*). Only COUNT may omit the field.
TEST_CASE("AdsAggregate remote wire: empty field for non-COUNT is rejected") {
    agg_wipe();
    auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);

    RemoteFixture fx;
    fx.open(dir);

    UNSIGNED8 forc[] = "";
    UNSIGNED8 spec[] = "SUM:";            // empty field, non-COUNT
    ADSHANDLE hRes   = 0;
    CHECK(AdsAggregate(fx.hTable, forc, spec, &hRes) != AE_SUCCESS);
    CHECK(hRes == 0);
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

TEST_CASE("AdsAggregate remote rejects oversized scans without moving cursor") {
    agg_wipe();
    const auto dir = agg_tmp_dir();
    seed_agg_fixture(dir);
    const auto path = dir / "agg.dbf";
    std::array<unsigned char, 32> header{};
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(file.good());
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        const std::uint32_t count = 100001;
        for (int i = 0; i < 4; ++i) header[4 + i] = static_cast<unsigned char>(count >> (8 * i));
        file.seekp(0);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
    }
    const std::uint16_t start = static_cast<std::uint16_t>(header[8] | (header[9] << 8));
    const std::uint16_t width = static_cast<std::uint16_t>(header[10] | (header[11] << 8));
    fs::resize_file(path, static_cast<std::uintmax_t>(start) + 100001u * width + 1);
    RemoteFixture fx;
    fx.open(dir);
    REQUIRE(AdsGotoRecord(fx.hTable, 2) == AE_SUCCESS);
    UNSIGNED8 predicate[] = "";
    UNSIGNED8 spec[] = "COUNT:";
    ADSHANDLE result = 0;
    CHECK(AdsAggregate(fx.hTable, predicate, spec, &result) != AE_SUCCESS);
    CHECK(result == 0);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(fx.hTable, ADS_IGNOREFILTERS, &recno) == AE_SUCCESS);
    CHECK(recno == 2);
}
