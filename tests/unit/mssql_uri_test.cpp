#include "doctest.h"
#if defined(OPENADS_WITH_MSSQL)
#include "sql_backend/mssql_uri.h"
using namespace openads::sql_backend;
TEST_CASE("parse mssql uri with port and percent-encoded password") {
    MssqlUri u;
    REQUIRE(parse_mssql_uri("mssql://sa:p%40ss@10.0.0.5:1433/openads_test", u));
    CHECK(u.host == "10.0.0.5");
    CHECK(u.port == 1433);
    CHECK(u.user == "sa");
    CHECK(u.password == "p@ss");          // %40 decoded
    CHECK(u.database == "openads_test");
}
TEST_CASE("parse mssql uri default port and tds scheme") {
    MssqlUri u;
    REQUIRE(parse_mssql_uri("tds://u:p@host/db", u));
    CHECK(u.port == 1433);
    CHECK(u.host == "host");
}
TEST_CASE("reject non-mssql uri") {
    MssqlUri u;
    CHECK(parse_mssql_uri("odbc://x", u) == false);
}
#endif
