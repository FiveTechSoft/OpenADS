// Tests for the thin SQL C API (include/openads/openads_sql.h).
// Exercises the full happy path against the native CDX/DBF engine in a temp
// data dir: connect -> CREATE/INSERT -> SELECT -> describe -> scrollable fetch
// -> column value, plus a prepared statement with a named bound parameter.
#include "doctest.h"
#include "openads/openads_sql.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string rtrim(const char* p, size_t n) {
    std::string s(p, n);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

TEST_CASE("openads_sql: connect, exec, describe, fetch, bind") {
    auto dir = fs::temp_directory_path() / "openads_sql_c_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    openads_conn* conn = nullptr;
    REQUIRE(openads_connect(dir.string().c_str(), "local",
                            nullptr, nullptr, &conn) == OPENADS_OK);
    REQUIRE(conn != nullptr);

    // DDL: create a table — no cursor attached.
    openads_stmt* st = nullptr;
    REQUIRE(openads_exec_direct(
        conn, "CREATE TABLE people (NAME Character(20), AGE Numeric(3,0))",
        &st) == OPENADS_OK);
    openads_finalize(st);
    st = nullptr;

    // DML: two rows.
    REQUIRE(openads_exec_direct(
        conn, "INSERT INTO people (NAME, AGE) VALUES ('alice', 30)", &st)
        == OPENADS_OK);
    openads_finalize(st);
    st = nullptr;
    REQUIRE(openads_exec_direct(
        conn, "INSERT INTO people (NAME, AGE) VALUES ('bob', 41)", &st)
        == OPENADS_OK);
    openads_finalize(st);
    st = nullptr;

    SUBCASE("describe + fetch all rows") {
        REQUIRE(openads_exec_direct(conn, "SELECT NAME, AGE FROM people", &st)
                == OPENADS_OK);

        int ncols = 0;
        REQUIRE(openads_num_cols(st, &ncols) == OPENADS_OK);
        CHECK(ncols == 2);

        char cname[64] = {0};
        REQUIRE(openads_col_name(st, 1, cname, sizeof(cname)) == OPENADS_OK);
        CHECK(std::string(cname) == "NAME");

        int ctype = 0;
        REQUIRE(openads_col_type(st, 1, &ctype) == OPENADS_OK);
        CHECK(ctype > 0);

        long rc = 0;
        REQUIRE(openads_row_count(st, &rc) == OPENADS_OK);
        CHECK(rc == 2);

        int seen = 0;
        char val[64] = {0};
        size_t vlen = 0;
        while (openads_fetch_next(st) == OPENADS_OK) {
            REQUIRE(openads_get_str(st, 1, val, sizeof(val), &vlen)
                    == OPENADS_OK);
            CHECK(vlen > 0);
            ++seen;
        }
        CHECK(seen == 2);
        CHECK(openads_fetch_next(st) == OPENADS_NO_DATA);
        openads_finalize(st);
        st = nullptr;
    }

    SUBCASE("fetch_absolute lands on the right row") {
        REQUIRE(openads_exec_direct(conn, "SELECT NAME FROM people", &st)
                == OPENADS_OK);
        char val[64] = {0};
        size_t vlen = 0;
        REQUIRE(openads_fetch_absolute(st, 2) == OPENADS_OK);
        REQUIRE(openads_get_str(st, 1, val, sizeof(val), &vlen) == OPENADS_OK);
        CHECK(rtrim(val, vlen) == "bob");
        openads_finalize(st);
        st = nullptr;
    }

    SUBCASE("prepared statement with a named numeric bind") {
        REQUIRE(openads_prepare(
            conn, "SELECT NAME, AGE FROM people WHERE AGE = :a", &st)
            == OPENADS_OK);
        int np = 0;
        REQUIRE(openads_num_params(st, &np) == OPENADS_OK);
        CHECK(np == 1);
        REQUIRE(openads_bind_int64(st, "a", 41) == OPENADS_OK);
        REQUIRE(openads_execute(st) == OPENADS_OK);

        int seen = 0;
        char val[64] = {0};
        size_t vlen = 0;
        while (openads_fetch_next(st) == OPENADS_OK) {
            REQUIRE(openads_get_str(st, 1, val, sizeof(val), &vlen)
                    == OPENADS_OK);
            CHECK(rtrim(val, vlen) == "bob");
            ++seen;
        }
        CHECK(seen == 1);
        openads_finalize(st);
        st = nullptr;
    }

    openads_disconnect(conn);
}

TEST_CASE("openads_sql: libversion is non-empty") {
    const char* v = openads_libversion();
    REQUIRE(v != nullptr);
    CHECK(std::strlen(v) > 0);
}
