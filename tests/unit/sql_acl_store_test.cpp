#include "doctest.h"

#include "sql_backend/sql_acl_store.h"

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>
#endif

using openads::sql_backend::SqlDdlDialect;
using openads::sql_backend::SqlQueryFn;

#if defined(OPENADS_WITH_SQLITE)

namespace {

SqlQueryFn sqlite_query_fn(sqlite3* db) {
    return [db](const std::string& sql)
        -> openads::util::Result<std::optional<std::string>> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(),
                               static_cast<int>(sql.size()),
                               &stmt, nullptr) != SQLITE_OK) {
            return openads::util::Error{
                5001, 0, sqlite3_errmsg(db), sql};
        }
        std::optional<std::string> out;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* txt = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 0));
            if (txt) out = txt;
        }
        sqlite3_finalize(stmt);
        return out;
    };
}

}  // namespace

TEST_CASE("sql_acl_store: effective_ops honors per-user grants") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);

    auto exec = [db](const std::string& sql) -> openads::util::Result<void> {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
            return openads::util::Result<void>{};
        }
        return openads::util::Error{5001, 0, sqlite3_errmsg(db), sql};
    };
    const auto qfn = sqlite_query_fn(db);

    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "REVOKE SELECT ON item FROM PUBLIC",
        SqlDdlDialect::Sqlite, exec));
    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT SELECT ON item TO alice",
        SqlDdlDialect::Sqlite, exec));

    const auto alice = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "alice", "item");
    CHECK(alice.select_);
    CHECK_FALSE(alice.insert_);

    const auto bob = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "bob", "item");
    CHECK_FALSE(bob.select_);

    sqlite3_close(db);
}

TEST_CASE("sql_acl_store: group membership expands effective_ops") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::member_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);

    auto exec = [db](const std::string& sql) -> openads::util::Result<void> {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
            return openads::util::Result<void>{};
        }
        return openads::util::Error{5001, 0, sqlite3_errmsg(db), sql};
    };
    const auto qfn = sqlite_query_fn(db);

    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT GROUP SALES TO alice", SqlDdlDialect::Sqlite, exec));
    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "REVOKE SELECT ON item FROM PUBLIC", SqlDdlDialect::Sqlite, exec));
    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT SELECT ON item TO SALES", SqlDdlDialect::Sqlite, exec));

    const auto alice = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "alice", "item");
    CHECK(alice.select_);

    const auto bob = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "bob", "item");
    CHECK_FALSE(bob.select_);

    sqlite3_close(db);
}

TEST_CASE("sql_acl_store: grantee lookup is case-insensitive") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);

    auto exec = [db](const std::string& sql) -> openads::util::Result<void> {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
            return openads::util::Result<void>{};
        }
        return openads::util::Error{5001, 0, sqlite3_errmsg(db), sql};
    };
    const auto qfn = sqlite_query_fn(db);

    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "REVOKE SELECT ON item FROM PUBLIC",
        SqlDdlDialect::Sqlite, exec));
    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT SELECT ON item TO Alice",
        SqlDdlDialect::Sqlite, exec));

    const auto alice = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "alice", "item");
    CHECK(alice.select_);

    const auto ALICE = openads::sql_backend::sql_acl_effective_ops(
        SqlDdlDialect::Sqlite, qfn, "ALICE", "item");
    CHECK(ALICE.select_);

    sqlite3_close(db);
}

TEST_CASE("sql_acl_store: connect user registry surfaces in catalog") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_exec(
        db, openads::sql_backend::member_table_ddl(SqlDdlDialect::Sqlite).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    auto exec = [db](const std::string& sql) -> openads::util::Result<void> {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
            return openads::util::Result<void>{};
        }
        return openads::util::Error{5001, 0, sqlite3_errmsg(db), sql};
    };
    openads::sql_backend::sql_acl_register_connect_user(
        SqlDdlDialect::Sqlite, exec, "bob");

    const auto users_sql =
        openads::sql_backend::acl_users_catalog_sql(SqlDdlDialect::Sqlite);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, users_sql.c_str(),
                               static_cast<int>(users_sql.size()),
                               &stmt, nullptr) == SQLITE_OK);
    bool saw_bob = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (user && std::string(user) == "bob") saw_bob = true;
    }
    sqlite3_finalize(stmt);
    CHECK(saw_bob);

    sqlite3_close(db);
}

TEST_CASE("sql_acl_store: catalog SQL lists members and users") {
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

    auto exec = [db](const std::string& sql) -> openads::util::Result<void> {
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK) {
            return openads::util::Result<void>{};
        }
        return openads::util::Error{5001, 0, sqlite3_errmsg(db), sql};
    };

    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT GROUP SALES TO carol", SqlDdlDialect::Sqlite, exec));
    REQUIRE(openads::sql_backend::try_sql_acl_statement(
        "GRANT SELECT ON item TO carol", SqlDdlDialect::Sqlite, exec));

    const auto members_sql =
        openads::sql_backend::acl_members_catalog_sql(SqlDdlDialect::Sqlite);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, members_sql.c_str(),
                               static_cast<int>(members_sql.size()),
                               &stmt, nullptr) == SQLITE_OK);
    bool saw_carol = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // SAP order: User_Name (col 0) then Group_Name (col 1).
        const char* user =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* group =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (user && group && std::string(user) == "carol" &&
            std::string(group) == "SALES") {
            saw_carol = true;
        }
    }
    sqlite3_finalize(stmt);
    CHECK(saw_carol);

    const auto users_sql =
        openads::sql_backend::acl_users_catalog_sql(SqlDdlDialect::Sqlite);
    REQUIRE(sqlite3_prepare_v2(db, users_sql.c_str(),
                             static_cast<int>(users_sql.size()),
                             &stmt, nullptr) == SQLITE_OK);
    bool saw_carol_user = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* user =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (user && std::string(user) == "carol") saw_carol_user = true;
    }
    sqlite3_finalize(stmt);
    CHECK(saw_carol_user);

    sqlite3_close(db);
}

#endif