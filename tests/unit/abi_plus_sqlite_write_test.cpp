// tests/unit/abi_plus_sqlite_write_test.cpp
// Navigational write on a SQLite-backed table: AdsAppendRecord +
// AdsSetString + AdsWriteRecord + AdsDeleteRecord. SQLite is in-process
// (vendored amalgamation), so this test is fully self-contained — it seeds a
// temp .db via the sqlite3 C API, then drives writes purely through the ACE
// ABI, mirroring the MariaDB/Postgres/Firebird navigational-write contract.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>

namespace {

void seed_clientes(const fs::path& db_path) {
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
    exec("CREATE TABLE clientes ("
         "id INTEGER PRIMARY KEY, nome TEXT, saldo REAL)");
    exec("INSERT INTO clientes (id, nome, saldo) VALUES "
         "(1,'Ana',10.5), (2,'Bob',NULL), (3,'Cid',0.0)");
    sqlite3_close(db);
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

struct WriteFixture {
    fs::path  dir;
    ADSHANDLE hConn = 0;
    ADSHANDLE hTable = 0;
    void open() {
        dir = fs::temp_directory_path() / "openads_write_sqlite";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        seed_clientes(dir / "clientes.db");
        const std::string uri = "sqlite://" + (dir / "clientes.db").string();
        std::vector<UNSIGNED8> srv(uri.size() + 1);
        std::memcpy(srv.data(), uri.c_str(), uri.size() + 1);
        REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        UNSIGNED8 tname[32] = "clientes";
        REQUIRE(AdsOpenTable(hConn, tname, tname, ADS_DEFAULT, 0, 0, 0,
                             ADS_DEFAULT, &hTable) == 0);
    }
    ~WriteFixture() {
        if (hTable) AdsCloseTable(hTable);
        if (hConn)  AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("ABI: sqlite AdsAppendRecord + AdsSetString + AdsWriteRecord + AdsDeleteRecord") {
    WriteFixture fx;
    fx.open();

    CHECK(row_count(fx.hTable) == 3);

    // Append a new row through the navigational ABI.
    REQUIRE(AdsAppendRecord(fx.hTable) == 0);
    set_str(fx.hTable, "id", "99");
    set_str(fx.hTable, "nome", "Dan");
    set_str(fx.hTable, "saldo", "42.5");
    REQUIRE(AdsWriteRecord(fx.hTable) == 0);
    CHECK(row_count(fx.hTable) == 4);

    REQUIRE(AdsGotoBottom(fx.hTable) == 0);
    CHECK(rtrim(field_str(fx.hTable, "nome")) == "Dan");

    // Update the current row.
    set_str(fx.hTable, "nome", "DanX");
    REQUIRE(AdsWriteRecord(fx.hTable) == 0);
    REQUIRE(AdsGotoBottom(fx.hTable) == 0);
    CHECK(rtrim(field_str(fx.hTable, "nome")) == "DanX");

    // Delete the current row.
    REQUIRE(AdsDeleteRecord(fx.hTable) == 0);
    CHECK(row_count(fx.hTable) == 3);
}

#endif // OPENADS_WITH_SQLITE
