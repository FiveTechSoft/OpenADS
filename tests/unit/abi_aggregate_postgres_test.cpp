// tests/unit/abi_aggregate_postgres_test.cpp
// Tier-3 aggregation push-down on a PostgreSQL-backed table. AdsAggregate
// translates the FOR predicate to SQL and runs one
// `SELECT COUNT/SUM/AVG/MIN/MAX ... WHERE` in PostgreSQL. Mirrors the SQLite
// aggregate test. Needs a live PostgreSQL (OPENADS_TEST_PG_URI, default
// 127.0.0.1:5433) — a no-op when the backend is compiled out.
#include "doctest.h"
#include "openads/ace.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>

namespace {

const char* test_pg_uri() {
    const char* e = std::getenv("OPENADS_TEST_PG_URI");
    return (e && e[0]) ? e : "postgresql://postgres@127.0.0.1:5433/postgres";
}

void seed_items(PGconn* conn) {
    auto exec = [&](const char* sql) {
        PGresult* r = PQexec(conn, sql);
        const auto st = PQresultStatus(r);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
            std::string msg = std::string("seed: ") + PQerrorMessage(conn);
            PQclear(r);
            FAIL(msg);
        }
        PQclear(r);
    };
    exec("DROP TABLE IF EXISTS aggitems");
    exec("CREATE TABLE aggitems (id INTEGER PRIMARY KEY, nm TEXT, qty INTEGER)");
    exec("INSERT INTO aggitems (id, nm, qty) VALUES "
         "(1,'a',50), (2,'b',100), (3,'c',150), (4,'d',200), (5,'e',10)");
}

std::string trim_right(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string agg_value(ADSHANDLE hRes, UNSIGNED32 i, UNSIGNED16& ty) {
    UNSIGNED8 buf[64]{};
    UNSIGNED16 len = sizeof(buf);
    ty = 0;
    REQUIRE(AdsAggregateValue(hRes, i, &ty, buf, &len) == AE_SUCCESS);
    return std::string(reinterpret_cast<char*>(buf), len);
}

struct PgFixture {
    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = 0;
    void open() {
        const char* uri_cstr = test_pg_uri();
        PGconn* seed = PQconnectdb(uri_cstr);
        REQUIRE(PQstatus(seed) == CONNECTION_OK);
        seed_items(seed);
        PQfinish(seed);

        std::vector<UNSIGNED8> srv(std::strlen(uri_cstr) + 1);
        std::memcpy(srv.data(), uri_cstr, std::strlen(uri_cstr) + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        UNSIGNED8 tname[32] = "aggitems";
        REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hTable) == 0);
    }
    ~PgFixture() {
        if (hTable) AdsCloseTable(hTable);
        if (hConn)  AdsDisconnect(hConn);
    }
};

} // namespace

TEST_CASE("Tier-3 push-down: AdsAggregate on a PostgreSQL table totals server-side") {
    PgFixture fx;
    fx.open();

    UNSIGNED8 forc[] = "QTY >= 100";   // rows 2,3,4 (qty 100,150,200)
    UNSIGNED8 spec[] = "COUNT:;SUM:QTY;AVG:QTY;MIN:QTY;MAX:QTY;MIN:NM;MAX:NM";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsAggregate(fx.hTable, forc, spec, &hRes) == AE_SUCCESS);
    CHECK(hRes != 0);

    UNSIGNED32 n = 0;
    REQUIRE(AdsAggregateCount(hRes, &n) == AE_SUCCESS);
    CHECK(n == 7u);

    UNSIGNED16 ty = 0;
    CHECK(agg_value(hRes, 0, ty) == "3");   CHECK(ty == 1u);  // COUNT(*)
    CHECK(agg_value(hRes, 1, ty) == "450"); CHECK(ty == 1u);  // SUM QTY
    CHECK(agg_value(hRes, 2, ty) == "150"); CHECK(ty == 1u);  // AVG QTY
    CHECK(agg_value(hRes, 3, ty) == "100"); CHECK(ty == 1u);  // MIN QTY
    CHECK(agg_value(hRes, 4, ty) == "200"); CHECK(ty == 1u);  // MAX QTY
    CHECK(trim_right(agg_value(hRes, 5, ty)) == "b"); CHECK(ty == 2u);  // MIN NM
    CHECK(trim_right(agg_value(hRes, 6, ty)) == "d"); CHECK(ty == 2u);  // MAX NM

    REQUIRE(AdsAggregateClose(hRes) == AE_SUCCESS);
}

TEST_CASE("Tier-3 push-down: PostgreSQL zero matches -> 0 / 0 / empty") {
    PgFixture fx;
    fx.open();

    UNSIGNED8 forc[] = "QTY > 1000";
    UNSIGNED8 spec[] = "COUNT:;SUM:QTY;AVG:QTY;MIN:NM";
    ADSHANDLE hRes   = 0;
    REQUIRE(AdsAggregate(fx.hTable, forc, spec, &hRes) == AE_SUCCESS);

    UNSIGNED16 ty = 0;
    CHECK(agg_value(hRes, 0, ty) == "0"); CHECK(ty == 1u);   // COUNT -> 0
    CHECK(agg_value(hRes, 1, ty) == "0"); CHECK(ty == 1u);   // SUM   -> 0
    agg_value(hRes, 2, ty);               CHECK(ty == 0u);   // AVG   -> empty
    agg_value(hRes, 3, ty);               CHECK(ty == 0u);   // MIN   -> empty

    REQUIRE(AdsAggregateClose(hRes) == AE_SUCCESS);
}

TEST_CASE("Tier-3 push-down: PostgreSQL untranslatable FOR declines") {
    PgFixture fx;
    fx.open();

    UNSIGNED8 forc[] = "RECNO() > 1";
    UNSIGNED8 spec[] = "COUNT:";
    ADSHANDLE hRes   = 0;
    CHECK(AdsAggregate(fx.hTable, forc, spec, &hRes) != AE_SUCCESS);
    CHECK(hRes == 0);
}

#else

TEST_CASE("Tier-3 aggregate push-down: postgresql backend disabled at compile time") {
    CHECK(true);
}

#endif
