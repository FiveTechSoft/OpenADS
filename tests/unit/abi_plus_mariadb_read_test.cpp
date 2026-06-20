#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(OPENADS_WITH_MARIADB)
#include "sql_backend/maria_uri.h"
#include <mysql.h>
#endif

#if defined(OPENADS_WITH_MARIADB)

namespace {

constexpr const char* kDefaultMariaUri =
    "mariadb://root@127.0.0.1:3306/test";

const char* test_maria_uri() {
    const char* uri_env = std::getenv("OPENADS_TEST_MARIADB_URI");
    if (uri_env != nullptr && uri_env[0] != '\0') {
        return uri_env;
    }
    return kDefaultMariaUri;
}

void seed_fixture(MYSQL* conn) {
    auto exec = [&](const char* sql) {
        if (mysql_query(conn, sql) != 0) {
            const char* msg = mysql_error(conn);
            std::string detail = "seed failed";
            if (msg != nullptr && msg[0] != '\0') {
                detail += ": ";
                detail += msg;
            }
            FAIL(detail);
        }
    };
    exec("DROP TABLE IF EXISTS clientes");
    exec("CREATE TABLE clientes ("
         "id INT PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE)");
    exec("INSERT INTO clientes (id, nome, saldo) VALUES "
         "(1, 'Ana', 10.5), (2, 'Bob', NULL), (3, 'Cid', 0.0)");
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[32];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[128] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

} // namespace

TEST_CASE("ABI: mariadb read-only AdsOpenTable navigation") {
    const char* uri_cstr = test_maria_uri();

    MYSQL* seed = mysql_init(nullptr);
    REQUIRE(seed != nullptr);
    openads::sql_backend::MariaUri muri;
    REQUIRE(openads::sql_backend::parse_maria_uri(uri_cstr, muri));
    const char* host = muri.host.empty() ? "127.0.0.1" : muri.host.c_str();
    const char* user = muri.user.empty() ? nullptr : muri.user.c_str();
    const char* pass = muri.password.empty() ? nullptr : muri.password.c_str();
    const char* db   = muri.database.empty() ? nullptr : muri.database.c_str();
    REQUIRE(mysql_real_connect(seed, host, user, pass, db, muri.port,
                               nullptr, 0) != nullptr);
    seed_fixture(seed);
    mysql_close(seed);

    const std::string uri = uri_cstr;
    std::vector<UNSIGNED8> srv(uri.size() + 1);
    std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl_name[32] = "clientes";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                         ADS_DEFAULT, 0, 0, 0, ADS_READONLY,
                         &hTable) == 0);

    UNSIGNED16 nfields = 0;
    REQUIRE(AdsGetNumFields(hTable, &nfields) == 0);
    CHECK(nfields == 3);

    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    CHECK(count == 3);

    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED16 bof = 1;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    CHECK(bof == 0);

    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Ana"));

    REQUIRE(AdsSkip(hTable, 1) == 0);
    CHECK(field_str(hTable, "saldo").empty());

    REQUIRE(AdsSkip(hTable, 1) == 0);
    CHECK(field_str(hTable, "nome") == std::string(64, ' ').replace(0, 3, "Cid"));

    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 0);

    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    CHECK(eof == 1);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

#else

TEST_CASE("ABI: mariadb backend disabled at compile time") {
    UNSIGNED8 uri[] = "mariadb://127.0.0.1/none";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}

#endif