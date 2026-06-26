// tests/unit/abi_aggregate_sqlite_test.cpp
// Tier-3 aggregation push-down on a SQLite-backed table. AdsAggregate
// translates the FOR predicate to SQL (engine::try_emit_sql_where) and runs a
// single `SELECT COUNT/TOTAL/AVG/MIN/MAX ... WHERE ...` in the backend — the
// totals are computed where the data lives, never row-by-row in the client.
// An untranslatable predicate declines (caller falls back), never half-applies.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>

namespace {

void seed_items(const fs::path& db_path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    auto exec = [&](const char* sql) {
        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            const std::string msg = err ? err : "exec failed";
            if (err) sqlite3_free(err);
            FAIL(msg);
        }
    };
    exec("CREATE TABLE items (id INTEGER PRIMARY KEY, nm TEXT, qty INTEGER)");
    exec("INSERT INTO items (id, nm, qty) VALUES "
         "(1,'a',50), (2,'b',100), (3,'c',150), (4,'d',200), (5,'e',10)");
    sqlite3_close(db);
}

struct SqliteFixture {
    fs::path  dir;
    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = 0;
    void open() {
        dir = fs::temp_directory_path() / "openads_agg_sqlite";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        seed_items(dir / "items.db");
        const std::string uri = "sqlite://" + (dir / "items.db").string();
        std::vector<UNSIGNED8> srv(uri.size() + 1);
        std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        UNSIGNED8 tname[32] = "items";
        REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_DEFAULT, 0, 0, 0,
                             ADS_READONLY, &hTable) == 0);
    }
    ~SqliteFixture() {
        if (hTable) AdsCloseTable(hTable);
        if (hConn)  AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

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

} // namespace

TEST_CASE("Tier-3 push-down: AdsAggregate on a SQLite table totals server-side") {
    SqliteFixture fx;
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

TEST_CASE("Tier-3 push-down: SQLite zero matches -> 0 / 0 / empty") {
    SqliteFixture fx;
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

TEST_CASE("Tier-3 push-down: untranslatable FOR declines (caller falls back)") {
    SqliteFixture fx;
    fx.open();

    UNSIGNED8 forc[] = "RECNO() > 1";   // try_emit_sql_where can't model RECNO()
    UNSIGNED8 spec[] = "COUNT:";
    ADSHANDLE hRes   = 0;
    CHECK(AdsAggregate(fx.hTable, forc, spec, &hRes) != AE_SUCCESS);
    CHECK(hRes == 0);
}

#else

TEST_CASE("Tier-3 aggregate push-down: sqlite backend disabled at compile time") {
    CHECK(true);
}

#endif
