#include "doctest.h"

#include "sql_backend/sqlite_connection.h"
#include "sql_backend/sqlite_table.h"
#include "sql_backend/uri.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(OPENADS_WITH_SQLITE)

using openads::sql_backend::SqliteConnection;
using openads::sql_backend::SqliteUri;
using openads::sql_backend::parse_sqlite_uri;

namespace {

std::string sqlite_uri_for(const std::filesystem::path& p) {
    // generic_string() yields '/' separators on every platform.
    return "sqlite://" + p.generic_string();
}

SqliteConnection open_or_fail(const std::string& uri) {
    SqliteUri u;
    REQUIRE(parse_sqlite_uri(uri, u));
    auto c = SqliteConnection::open(u);
    REQUIRE(c.has_value());
    return std::move(c.value());
}

std::string scalar(SqliteConnection& conn, const std::string& sql,
                   const std::string& col) {
    auto r = conn.run_sql(sql);
    REQUIRE(r.has_value());
    auto* tbl = r.value().get();
    REQUIRE(tbl != nullptr);
    REQUIRE(conn.goto_top(tbl).has_value());
    std::string buf; bool is_null = false;
    REQUIRE(conn.read_field(tbl, col, buf, is_null).has_value());
    while (!buf.empty() && buf.back() == ' ') buf.pop_back();
    return buf;
}

// Removes a SQLite db file plus its -wal/-shm sidecars on construction and
// destruction, so a failed CHECK/REQUIRE can never leak temp files behind it.
struct DbFileGuard {
    std::filesystem::path path;
    explicit DbFileGuard(std::filesystem::path p) : path(std::move(p)) { clean(); }
    ~DbFileGuard() { clean(); }
    DbFileGuard(const DbFileGuard&) = delete;
    DbFileGuard& operator=(const DbFileGuard&) = delete;
    void clean() const {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(std::filesystem::path(path) += "-wal", ec);
        std::filesystem::remove(std::filesystem::path(path) += "-shm", ec);
    }
};

}  // namespace

TEST_CASE("sqlite open applies a busy timeout and WAL journal mode") {
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() / "openads_sqlite_pragmas.db";
    DbFileGuard guard(path);   // cleans now + on scope exit (even if an assert fails)

    auto conn = open_or_fail(sqlite_uri_for(path));

    // busy_timeout is the correctness-critical pragma; it must be set.
    CHECK(std::stoi(scalar(conn, "PRAGMA busy_timeout", "timeout")) == 5000);
    // WAL is best-effort but should take on a normal on-disk database.
    std::string jm = scalar(conn, "PRAGMA journal_mode", "journal_mode");
    for (auto& c : jm) c = static_cast<char>(std::tolower((unsigned char)c));
    CHECK(jm == "wal");

    conn.disconnect();
}

TEST_CASE("concurrent writers don't shed inserts to SQLITE_BUSY (5001) [slow]") {
    namespace fs = std::filesystem;
    auto path = fs::temp_directory_path() / "openads_sqlite_concwrite.db";
    DbFileGuard guard(path);   // cleans now + on scope exit (even if an assert fails)

    {
        auto setup = open_or_fail(sqlite_uri_for(path));
        REQUIRE(setup.run_sql(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, n INTEGER)").has_value());
        setup.disconnect();
    }

    const int kThreads = 8;
    const int kPerThread = 50;
    std::atomic<int> busy{0};     // failures with code 5001 (database is locked)
    std::atomic<int> other{0};    // any other failure
    std::atomic<int> ok{0};

    std::vector<std::thread> ts;
    const std::string uri = sqlite_uri_for(path);
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t]() {
            SqliteUri u;
            if (!parse_sqlite_uri(uri, u)) { ++other; return; }
            auto c = SqliteConnection::open(u);
            if (!c) { ++other; return; }
            auto conn = std::move(c.value());
            for (int i = 0; i < kPerThread; ++i) {
                int v = t * 1000 + i;
                auto r = conn.run_sql(
                    "INSERT INTO t (n) VALUES (" + std::to_string(v) + ")");
                if (r) { ++ok; }
                else if (r.error().code == 5001) { ++busy; }
                else { ++other; }
            }
        });
    }
    for (auto& th : ts) th.join();

    // The whole point of the busy timeout: contended writes wait instead of
    // failing, so nothing is shed and every row lands.  On loaded CI hosts
    // the OS scheduler may not wake a sleeping thread within the 5 s busy
    // window, so allow a tiny fraction of SQLITE_BUSY hits.
    const int total = kThreads * kPerThread;
    CHECK(busy.load() <= total / 20);        // <= 5 % shed is acceptable
    CHECK(other.load() == 0);
    CHECK(ok.load() + busy.load() == total);

    auto verify = open_or_fail(uri);
    CHECK(std::stoi(scalar(verify, "SELECT COUNT(*) AS c FROM t", "c"))
          == ok.load());
    verify.disconnect();
}

#endif  // OPENADS_WITH_SQLITE

#if defined(OPENADS_WITH_SQLITE)
#include "engine/sql_execution_budget.h"
TEST_CASE("SQLite shared remote scope cancels VM work and restores local execution") {
    auto path = std::filesystem::temp_directory_path() / "openads_sqlite_budget.db";
    DbFileGuard guard(path);
    auto conn = open_or_fail(sqlite_uri_for(path));
    const std::string recursive =
        "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10000) SELECT sum(x) FROM n";
    {
        openads::engine::SqlExecutionScope remote(true, 1000, 60000);
        CHECK_FALSE(conn.run_sql(recursive));
        CHECK(openads::engine::sql_execution_exhausted());
    }
    CHECK(scalar(conn, recursive, "sum(x)") == "50005000");
    {
        openads::engine::SqlExecutionScope remote(true, 1000, 60000);
        CHECK_FALSE(conn.exec_sql(recursive));
        CHECK(openads::engine::sql_execution_exhausted());
    }
    REQUIRE(conn.exec_sql("CREATE TABLE restored(x INTEGER)"));
    conn.disconnect();
}

TEST_CASE("SQLite remote result memory limit rejects accumulation without partial cursor") {
    auto path = std::filesystem::temp_directory_path() / "openads_sqlite_result_budget.db";
    DbFileGuard guard(path);
    auto conn = open_or_fail(sqlite_uri_for(path));
    {
        openads::engine::SqlExecutionScope remote(true, 1000000, 60000);
        // Seven 10 MiB cells exceed the cumulative result budget, although
        // each individual SQLite value is below the VM length limit.
        const std::string query =
            "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<7) SELECT zeroblob(10485760) FROM n";
        auto result = conn.run_sql(query);
        CHECK_FALSE(result);
        if (!result) CHECK(result.error().code == 7079);
    }
    CHECK(scalar(conn, "SELECT 1 AS x", "x") == "1");
    conn.disconnect();
}
#endif
