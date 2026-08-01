#include "doctest.h"

#include "sql_backend/sql_acl_store.h"
#include "sql_backend/sql_system_catalog.h"

using openads::sql_backend::SqlDdlDialect;
using openads::sql_backend::build_system_catalog_sql;
using openads::sql_backend::rewrite_system_select_sql;

TEST_CASE("sql_system_catalog: views/triggers/procs/functions/links on SQLite") {
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "views").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "triggers").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "storedprocedures")
              .has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "functions").has_value());
    CHECK(build_system_catalog_sql(SqlDdlDialect::Sqlite, "links").has_value());
}

TEST_CASE("sql_system_catalog: rewrite preserves WHERE on system.permissions") {
    const char* sql =
        "SELECT OBJ_NAME FROM system.permissions "
        "WHERE OBJ_NAME = 'item' AND OBJ_TYPE = '1'";
    auto rewritten =
        rewrite_system_select_sql(SqlDdlDialect::Sqlite, sql);
    REQUIRE(rewritten.has_value());
    CHECK(rewritten->find("WHERE OBJ_NAME = 'item'") != std::string::npos);
    CHECK(rewritten->find("_openads_sys") != std::string::npos);
}

TEST_CASE("sql_system_catalog: non-system SELECT is not rewritten") {
    CHECK_FALSE(rewrite_system_select_sql(
        SqlDdlDialect::Sqlite, "SELECT * FROM items").has_value());
}

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>

TEST_CASE("sql_system_catalog: rewritten permissions WHERE executes on SQLite") {
    const char* sql =
        "SELECT OBJ_NAME FROM system.permissions "
        "WHERE OBJ_NAME = 'item' AND OBJ_TYPE = '1'";
    auto rewritten =
        rewrite_system_select_sql(SqlDdlDialect::Sqlite, sql);
    REQUIRE(rewritten.has_value());

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(db,
                         "CREATE TABLE item (GRP TEXT, DATA TEXT)",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    const int prep = sqlite3_prepare_v2(
        db, rewritten->c_str(),
        static_cast<int>(rewritten->size()), &stmt, nullptr);
    if (prep != SQLITE_OK) {
        MESSAGE("prepare failed: ", sqlite3_errmsg(db));
        MESSAGE("sql: ", *rewritten);
    }
    REQUIRE(prep == SQLITE_OK);
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) ++rows;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    CHECK(rows == 1);
}

TEST_CASE("sql_system_catalog: users/groups/members catalog SQL on SQLite") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::member_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::user_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, "INSERT INTO OPENADS$MEMBER (user_name, group_name) "
            "VALUES ('carol', 'SALES')",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, "INSERT INTO OPENADS$USER (user_name) VALUES ('dave')",
        nullptr, nullptr, nullptr) == SQLITE_OK);

    auto users = build_system_catalog_sql(SqlDdlDialect::Sqlite, "users");
    auto groups = build_system_catalog_sql(SqlDdlDialect::Sqlite, "usergroups");
    auto members =
        build_system_catalog_sql(SqlDdlDialect::Sqlite, "usergroupmembers");
    REQUIRE(users.has_value());
    REQUIRE(groups.has_value());
    REQUIRE(members.has_value());

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, users->c_str(),
                               static_cast<int>(users->size()),
                               &stmt, nullptr) == SQLITE_OK);
    bool saw_carol = false;
    bool saw_dave = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* u =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (!u) continue;
        if (std::string(u) == "carol") saw_carol = true;
        if (std::string(u) == "dave") saw_dave = true;
    }
    sqlite3_finalize(stmt);
    CHECK(saw_carol);
    CHECK(saw_dave);

    REQUIRE(sqlite3_prepare_v2(db, groups->c_str(),
                               static_cast<int>(groups->size()),
                               &stmt, nullptr) == SQLITE_OK);
    bool saw_sales = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* g =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (g && std::string(g) == "SALES") saw_sales = true;
    }
    sqlite3_finalize(stmt);
    CHECK(saw_sales);

    REQUIRE(sqlite3_prepare_v2(db, members->c_str(),
                               static_cast<int>(members->size()),
                               &stmt, nullptr) == SQLITE_OK);
    bool saw_pair = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // SAP's system.usergroupmembers order is User_Name THEN Group_Name,
        // so column 0 is the user and column 1 is the group.
        const char* u =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* g =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (g && u && std::string(g) == "SALES" &&
            std::string(u) == "carol") {
            saw_pair = true;
        }
    }
    sqlite3_finalize(stmt);
    CHECK(saw_pair);

    sqlite3_close(db);
}
#endif