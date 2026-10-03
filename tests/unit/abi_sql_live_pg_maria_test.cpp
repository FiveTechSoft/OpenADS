// Live SQL backend smoke: prepared statements + system.* catalog.
// Skips unless the matching OPENADS_TEST_* env var reaches a server/DB.
#include "doctest.h"
#include "openads/ace.h"
#include "engine/sql_execution_budget.h"
#include "sql_backend/postgres_connection.h"
#include "sql_backend/maria_connection.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int sql_count(ADSHANDLE hConn, const char* sql) {
    ADSHANDLE stmt = 0;
    if (AdsCreateSQLStatement(hConn, &stmt) != 0) return -1;
    std::vector<UNSIGNED8> buf(std::strlen(sql) + 1);
    std::memcpy(buf.data(), sql, buf.size());
    ADSHANDLE cur = 0;
    if (AdsExecuteSQLDirect(stmt, buf.data(), &cur) != 0) {
        AdsCloseSQLStatement(stmt);
        return -1;
    }
    UNSIGNED32 cnt = 0;
    AdsGetRecordCount(cur, 0, &cnt);
    if (cur != 0) AdsCloseTable(cur);
    AdsCloseSQLStatement(stmt);
    return static_cast<int>(cnt);
}

}  // namespace

#if defined(OPENADS_WITH_POSTGRESQL)

TEST_CASE("SQL live PG: remote streamed budgets and connection failure") {
    using namespace openads;
    const char* uri = std::getenv("OPENADS_TEST_PG_URI");
    if (uri == nullptr || uri[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_PG_URI not set");
        return;
    }
    sql_backend::PostgresUri parsed;
    REQUIRE(sql_backend::parse_postgres_uri(uri, parsed));
    SUBCASE("stream and final command result") {
        auto opened = sql_backend::PostgresConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        {
            engine::SqlExecutionScope scope(true);
            auto result = conn.run_sql("SELECT 'first'; SELECT 'last'");
            REQUIRE(result);
            REQUIRE(result.value());
            REQUIRE(result.value()->result_rows.size() == 1);
            CHECK(result.value()->result_rows[0][0] == "last");
            REQUIRE(conn.exec_sql("SELECT generate_series(1, 10)"));
        }
        CHECK(conn.valid());
        REQUIRE(conn.run_sql("SELECT 1"));
    }
    SUBCASE("row cap closes only affected connection") {
        auto opened = sql_backend::PostgresConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        engine::SqlExecutionScope scope(true);
        auto result = conn.run_sql("SELECT generate_series(1, 100001)");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
    }
    SUBCASE("byte cap before retained copies") {
        auto opened = sql_backend::PostgresConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        engine::SqlExecutionScope scope(true);
        auto result = conn.run_sql(
            "SELECT repeat('x', 10 * 1024 * 1024) FROM generate_series(1, 7)");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
    }
    SUBCASE("busy server exceeds shared deadline") {
        auto opened = sql_backend::PostgresConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        const auto start = std::chrono::steady_clock::now();
        engine::SqlExecutionScope scope(true, 1000000, 30);
        auto result = conn.exec_sql("SELECT pg_sleep(2)");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    }
}

TEST_CASE("SQL live PG: prepared INSERT + system.tables AdsOpenTable") {
    const char* uri = std::getenv("OPENADS_TEST_PG_URI");
    if (uri == nullptr || uri[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_PG_URI not set");
        return;
    }
    std::vector<UNSIGNED8> srv(std::strlen(uri) + 1);
    std::memcpy(srv.data(), uri, srv.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 drop[] = "DROP TABLE IF EXISTS live_smoke";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    UNSIGNED8 create[] =
        "CREATE TABLE live_smoke (id INTEGER PRIMARY KEY, nome TEXT)";
    REQUIRE(AdsExecuteSQLDirect(hStmt, create, &hCur) == 0);

    UNSIGNED8 ins[] =
        "INSERT INTO live_smoke (id, nome) VALUES (:id, :nome)";
    REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
    UNSIGNED8 pid[] = "id";
    UNSIGNED8 pnome[] = "nome";
    REQUIRE(AdsSetDouble(hStmt, pid, 1.0) == 0);
    REQUIRE(AdsSetString(hStmt, pnome, (UNSIGNED8*)"alpha", 5) == 0);
    REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM live_smoke") == 1);

    UNSIGNED8 sysname[] = "system.tables";
    ADSHANDLE hSys = 0;
    REQUIRE(AdsOpenTable(hConn, sysname, sysname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hSys) == 0);
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hSys, 0, &n) == 0);
    CHECK(n >= 1u);
    REQUIRE(AdsCloseTable(hSys) == 0);

    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
}

#endif

#if defined(OPENADS_WITH_MARIADB)

TEST_CASE("SQL live Maria: remote streamed budgets and connection failure") {
    using namespace openads;
    const char* uri = std::getenv("OPENADS_TEST_MARIADB_URI");
    if (uri == nullptr || uri[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_MARIADB_URI not set");
        return;
    }
    sql_backend::MariaUri parsed;
    REQUIRE(sql_backend::parse_maria_uri(uri, parsed));
    SUBCASE("streaming and subsequent local query") {
        auto opened = sql_backend::MariaConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        {
            engine::SqlExecutionScope scope(true);
            auto result = conn.run_sql("SELECT 'last'");
            REQUIRE(result);
            REQUIRE(result.value());
            REQUIRE(result.value()->result_rows.size() == 1);
            CHECK(result.value()->result_rows[0][0] == "last");
            REQUIRE(conn.exec_sql("SELECT 1"));
        }
        CHECK(conn.valid());
        REQUIRE(conn.run_sql("SELECT 1"));
    }
    SUBCASE("row cap closes connection") {
        auto opened = sql_backend::MariaConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        engine::SqlExecutionScope scope(true);
        auto result = conn.run_sql("SELECT seq FROM seq_1_to_100001");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
    }
    SUBCASE("byte cap before retained copies") {
        auto opened = sql_backend::MariaConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        engine::SqlExecutionScope scope(true);
        auto result = conn.run_sql("SELECT REPEAT('x', 10 * 1024 * 1024) FROM seq_1_to_7");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
    }
    SUBCASE("busy server exceeds shared deadline") {
        auto opened = sql_backend::MariaConnection::open(parsed);
        REQUIRE(opened);
        auto& conn = opened.value();
        const auto start = std::chrono::steady_clock::now();
        engine::SqlExecutionScope scope(true, 1000000, 30);
        auto result = conn.exec_sql("SELECT SLEEP(2)");
        REQUIRE_FALSE(result);
        CHECK(result.error().code == 7079);
        CHECK_FALSE(conn.valid());
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    }
}

TEST_CASE("SQL live Maria: prepared INSERT + system.columns SQL") {
    const char* uri = std::getenv("OPENADS_TEST_MARIADB_URI");
    if (uri == nullptr || uri[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_MARIADB_URI not set");
        return;
    }
    std::vector<UNSIGNED8> srv(std::strlen(uri) + 1);
    std::memcpy(srv.data(), uri, srv.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 drop[] = "DROP TABLE IF EXISTS live_smoke";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    UNSIGNED8 create[] =
        "CREATE TABLE live_smoke (id INT PRIMARY KEY, nome VARCHAR(40))";
    REQUIRE(AdsExecuteSQLDirect(hStmt, create, &hCur) == 0);

    UNSIGNED8 ins[] =
        "INSERT INTO live_smoke (id, nome) VALUES (:id, :nome)";
    REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
    UNSIGNED8 pid[] = "id";
    UNSIGNED8 pnome[] = "nome";
    REQUIRE(AdsSetDouble(hStmt, pid, 2.0) == 0);
    REQUIRE(AdsSetString(hStmt, pnome, (UNSIGNED8*)"beta", 4) == 0);
    REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM live_smoke") == 1);
    CHECK(sql_count(hConn, "SELECT * FROM system.columns") >= 2);

    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
}

#endif

#if defined(OPENADS_WITH_MSSQL)

TEST_CASE("SQL live MSSQL: prepared INSERT + system.tables AdsOpenTable") {
    const char* uri = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
    if (uri == nullptr || uri[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_MSSQL_CONNSTR not set");
        return;
    }
    std::vector<UNSIGNED8> srv(std::strlen(uri) + 1);
    std::memcpy(srv.data(), uri, srv.size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    UNSIGNED8 drop[] =
        "IF OBJECT_ID('live_smoke', 'U') IS NOT NULL DROP TABLE live_smoke";
    ADSHANDLE hCur = 0;
    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    UNSIGNED8 create[] =
        "CREATE TABLE live_smoke (id INT PRIMARY KEY, nome NVARCHAR(40))";
    REQUIRE(AdsExecuteSQLDirect(hStmt, create, &hCur) == 0);

    UNSIGNED8 ins[] =
        "INSERT INTO live_smoke (id, nome) VALUES (:id, :nome)";
    REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
    UNSIGNED8 pid[] = "id";
    UNSIGNED8 pnome[] = "nome";
    REQUIRE(AdsSetDouble(hStmt, pid, 3.0) == 0);
    REQUIRE(AdsSetString(hStmt, pnome, (UNSIGNED8*)"gamma", 5) == 0);
    REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM live_smoke") == 1);

    UNSIGNED8 sysname[] = "system.tables";
    ADSHANDLE hSys = 0;
    REQUIRE(AdsOpenTable(hConn, sysname, sysname, ADS_DEFAULT, 0, 0, 0,
                         ADS_READONLY, &hSys) == 0);
    UNSIGNED32 n = 0;
    REQUIRE(AdsGetRecordCount(hSys, 0, &n) == 0);
    CHECK(n >= 1u);
    REQUIRE(AdsCloseTable(hSys) == 0);

    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
}

#endif

#if defined(OPENADS_WITH_FIREBIRD)

TEST_CASE("SQL live Firebird: prepared INSERT + system.columns SQL") {
    const char* db = std::getenv("OPENADS_TEST_FIREBIRD_DB");
    if (db == nullptr || db[0] == '\0') {
        MESSAGE("skip: OPENADS_TEST_FIREBIRD_DB not set");
        return;
    }
    const std::string uri = std::string("firebird:///") + db;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    ADSHANDLE hCur = 0;
    UNSIGNED8 drop[] = "DROP TABLE live_smoke";
    AdsExecuteSQLDirect(hStmt, drop, &hCur);
    UNSIGNED8 create[] =
        "CREATE TABLE live_smoke (id INTEGER NOT NULL PRIMARY KEY, "
        "nome VARCHAR(40))";
    REQUIRE(AdsExecuteSQLDirect(hStmt, create, &hCur) == 0);

    UNSIGNED8 ins[] =
        "INSERT INTO live_smoke (id, nome) VALUES (:id, :nome)";
    REQUIRE(AdsPrepareSQL(hStmt, ins) == 0);
    UNSIGNED8 pid[] = "id";
    UNSIGNED8 pnome[] = "nome";
    REQUIRE(AdsSetDouble(hStmt, pid, 4.0) == 0);
    REQUIRE(AdsSetString(hStmt, pnome, (UNSIGNED8*)"delta", 5) == 0);
    REQUIRE(AdsExecuteSQL(hStmt, &hCur) == 0);

    CHECK(sql_count(hConn, "SELECT * FROM live_smoke") == 1);
    CHECK(sql_count(hConn, "SELECT * FROM system.columns") >= 2);

    REQUIRE(AdsExecuteSQLDirect(hStmt, drop, &hCur) == 0);
    AdsCloseSQLStatement(hStmt);
    AdsDisconnect(hConn);
}

#endif