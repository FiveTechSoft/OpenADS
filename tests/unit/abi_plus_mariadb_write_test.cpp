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

namespace {

constexpr const char* kDefaultMariaUri =
    "mariadb://root@127.0.0.1:3306/test";

const char* test_maria_uri() {
    const char* uri_env = std::getenv("OPENADS_TEST_MARIADB_URI");
    if (uri_env != nullptr && uri_env[0] != '\0') return uri_env;
    return kDefaultMariaUri;
}

// Connect via the mysql C API; returns nullptr if unreachable (test skips).
MYSQL* connect_seed(const char* uri_cstr) {
    openads::sql_backend::MariaUri muri;
    if (!openads::sql_backend::parse_maria_uri(uri_cstr, muri)) return nullptr;
    MYSQL* c = mysql_init(nullptr);
    if (c == nullptr) return nullptr;
    const char* host = muri.host.empty() ? "127.0.0.1" : muri.host.c_str();
    const char* user = muri.user.empty() ? nullptr : muri.user.c_str();
    const char* pass = muri.password.empty() ? nullptr : muri.password.c_str();
    const char* db   = muri.database.empty() ? nullptr : muri.database.c_str();
    if (mysql_real_connect(c, host, user, pass, db, muri.port, nullptr, 0)
            == nullptr) {
        mysql_close(c);
        return nullptr;
    }
    return c;
}

void seed_fixture(MYSQL* conn) {
    auto exec = [&](const char* sql) {
        if (mysql_query(conn, sql) != 0) {
            const char* msg = mysql_error(conn);
            std::string detail = "seed failed";
            if (msg != nullptr && msg[0] != '\0') { detail += ": "; detail += msg; }
            FAIL(detail);
        }
    };
    exec("DROP TABLE IF EXISTS clientes");
    exec("CREATE TABLE clientes ("
         "id INT PRIMARY KEY, nome VARCHAR(64), saldo DOUBLE,"
         " activo TINYINT)");
    exec("INSERT INTO clientes (id, nome, saldo, activo) VALUES "
         "(1, 'Ana', 10.5, 1), (2, 'Bob', NULL, 0), (3, 'Cid', 0.0, 1)");
}

std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string field_str(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[32];
    std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[256] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

void set_str(ADSHANDLE hTable, const char* field, const char* value) {
    UNSIGNED8 f[64];
    std::memcpy(f, field, std::strlen(field) + 1);
    UNSIGNED8 v[256];
    std::memcpy(v, value, std::strlen(value) + 1);
    REQUIRE(AdsSetString(hTable, f, v,
                         static_cast<UNSIGNED32>(std::strlen(value))) == 0);
}

UNSIGNED32 row_count(ADSHANDLE hTable) {
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &count) == 0);
    return count;
}

} // namespace

TEST_CASE("ABI: mariadb AdsAppendRecord + AdsSetString + AdsWriteRecord + AdsDeleteRecord") {
    const char* uri_cstr = test_maria_uri();
    MYSQL* seed = connect_seed(uri_cstr);
    if (seed == nullptr) {
        MESSAGE("MariaDB not reachable; skipping live write test");
        return;
    }
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
                         ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);

    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsAppendRecord(hTable) == 0);
    set_str(hTable, "id", "99");
    set_str(hTable, "nome", "Dan");
    set_str(hTable, "saldo", "42.5");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 4);

    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "Dan");

    set_str(hTable, "nome", "DanX");
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsGotoBottom(hTable) == 0);
    CHECK(rtrim(field_str(hTable, "nome")) == "DanX");

    REQUIRE(AdsDeleteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

TEST_CASE("ABI: mariadb AdsSetDouble / AdsSetLong / AdsSetLogical write path") {
    const char* uri_cstr = test_maria_uri();
    MYSQL* seed = connect_seed(uri_cstr);
    if (seed == nullptr) {
        MESSAGE("MariaDB not reachable; skipping live setter test");
        return;
    }
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
                         ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);

    UNSIGNED8 f_id[8] = "id";
    UNSIGNED8 f_saldo[16] = "saldo";
    UNSIGNED8 f_activo[16] = "activo";

    // INSERT path: append a row through the numeric/logical setters.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    REQUIRE(AdsSetLong(hTable, f_id, 100) == 0);
    set_str(hTable, "nome", "Eve");
    REQUIRE(AdsSetDouble(hTable, f_saldo, 7.25) == 0);
    REQUIRE(AdsSetLogical(hTable, f_activo, 1) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 4);

    REQUIRE(AdsGotoBottom(hTable) == 0);
    SIGNED32 lval = 0;
    double dval = 0.0;
    UNSIGNED16 bval = 0;
    REQUIRE(AdsGetLong(hTable, f_id, &lval) == 0);
    CHECK(lval == 100);
    REQUIRE(AdsGetDouble(hTable, f_saldo, &dval) == 0);
    CHECK(dval == doctest::Approx(7.25));
    REQUIRE(AdsGetLogical(hTable, f_activo, &bval) == 0);
    CHECK(bval == 1);

    // UPDATE path on the positioned row.
    REQUIRE(AdsSetDouble(hTable, f_saldo, 8.5) == 0);
    REQUIRE(AdsSetLogical(hTable, f_activo, 0) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);
    REQUIRE(AdsGotoBottom(hTable) == 0);
    REQUIRE(AdsGetDouble(hTable, f_saldo, &dval) == 0);
    CHECK(dval == doctest::Approx(8.5));
    REQUIRE(AdsGetLogical(hTable, f_activo, &bval) == 0);
    CHECK(bval == 0);

    REQUIRE(AdsDeleteRecord(hTable) == 0);
    CHECK(row_count(hTable) == 3);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

TEST_CASE("ABI: mariadb AdsLockRecord is cross-connection (named lock)") {
    const char* uri_cstr = test_maria_uri();
    MYSQL* seed = connect_seed(uri_cstr);
    if (seed == nullptr) {
        MESSAGE("MariaDB not reachable; skipping live lock test");
        return;
    }
    seed_fixture(seed);
    mysql_close(seed);

    const std::string uri = uri_cstr;
    auto open_clientes = [&](ADSHANDLE& hConn) -> ADSHANDLE {
        std::vector<UNSIGNED8> srv(uri.size() + 1);
        std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 tbl_name[32] = "clientes";
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                             ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);
        return hTable;
    };

    ADSHANDLE hConnA = 0, hConnB = 0;
    ADSHANDLE hA = open_clientes(hConnA);   // two distinct MariaDB sessions
    ADSHANDLE hB = open_clientes(hConnB);

    CHECK(AdsLockRecord(hA, 1) == 0);
    CHECK(AdsLockRecord(hB, 1) != 0);       // refused: A holds it
    CHECK(AdsUnlockRecord(hA, 1) == 0);
    CHECK(AdsLockRecord(hB, 1) == 0);       // now free
    CHECK(AdsUnlockRecord(hB, 1) == 0);

    REQUIRE(AdsCloseTable(hA) == 0);
    REQUIRE(AdsCloseTable(hB) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
}

TEST_CASE("ABI: mariadb AdsLockTable is cross-connection (named lock)") {
    const char* uri_cstr = test_maria_uri();
    MYSQL* seed = connect_seed(uri_cstr);
    if (seed == nullptr) {
        MESSAGE("MariaDB not reachable; skipping live table-lock test");
        return;
    }
    seed_fixture(seed);
    mysql_close(seed);

    const std::string uri = uri_cstr;
    auto open_clientes = [&](ADSHANDLE& hConn) -> ADSHANDLE {
        std::vector<UNSIGNED8> srv(uri.size() + 1);
        std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 tbl_name[32] = "clientes";
        ADSHANDLE hTable = 0;
        REQUIRE(AdsOpenTable(hConn, tbl_name, tbl_name,
                             ADS_DEFAULT, 0, 0, 0, ADS_DEFAULT, &hTable) == 0);
        return hTable;
    };

    ADSHANDLE hConnA = 0, hConnB = 0;
    ADSHANDLE hA = open_clientes(hConnA);   // two distinct MariaDB sessions
    ADSHANDLE hB = open_clientes(hConnB);

    CHECK(AdsLockTable(hA) == 0);
    CHECK(AdsLockTable(hB) != 0);           // refused: A holds the table lock
    CHECK(AdsUnlockTable(hA) == 0);
    CHECK(AdsLockTable(hB) == 0);           // now free
    CHECK(AdsUnlockTable(hB) == 0);

    REQUIRE(AdsCloseTable(hA) == 0);
    REQUIRE(AdsCloseTable(hB) == 0);
    REQUIRE(AdsDisconnect(hConnA) == 0);
    REQUIRE(AdsDisconnect(hConnB) == 0);
}

#endif
