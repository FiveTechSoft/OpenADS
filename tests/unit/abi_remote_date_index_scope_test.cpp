// REMOTE index on a DBF Date field must build AND scope correctly.
// Field report: "REMOTE will still NOT build an index on the dbf DATE
// fields. Thus, SetScope will also not work."
// Repro over the wire: create table with a Date column, remote
// AdsCreateIndex61 on the bare date field, then AdsSetScope TOP/BOTTOM
// with YYYYMMDD string keys and verify scoped navigation.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

void make_date_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "NOME,Character,20;WHEN,Date,8";
    UNSIGNED8 tname[]  = "dtidx.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);

    struct Row { const char* nome; const char* when; };
    const Row rows[] = {
        {"Alpha", "2024-01-15"},
        {"Beta",  "2025-06-20"},
        {"Gamma", "2026-12-31"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fNome[] = "NOME";
        AdsSetString(hT, fNome, (UNSIGNED8*)r.nome,
                     static_cast<UNSIGNED32>(std::strlen(r.nome)));
        UNSIGNED8 fWhen[] = "WHEN";
        AdsSetDate(hT, fWhen, (UNSIGNED8*)r.when,
                   static_cast<UNSIGNED16>(std::strlen(r.when)));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

std::string read_field(ADSHANDLE h, const char* name) {
    UNSIGNED8 out[64] = {0};
    UNSIGNED32 cap = sizeof(out);
    UNSIGNED8 f[32];
    std::memcpy(f, name, std::strlen(name) + 1);
    REQUIRE(AdsGetString(h, f, out, &cap, 0) == 0);
    return std::string(reinterpret_cast<char*>(out), cap);
}

// Julian day number (same convention as hb_dateEncode).
long jdn(int y, int m, int d) {
    long a = (14 - m) / 12;
    long yy = y + 4800 - a;
    long mm = m + 12 * a - 3;
    return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 +
           yy / 400 - 32045;
}

} // namespace

TEST_CASE("remote index on DBF date field builds and scopes") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_date_idx";
    make_date_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "dtidx.dbf";
    UNSIGNED8 alias[] = "DTIDX";
    REQUIRE(AdsOpenTable(hRC, tname, alias, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);

    // Build an index tag on the DATE field over the wire.
    UNSIGNED8 bag[]  = "dtidx.cdx";
    UNSIGNED8 tag[]  = "TWHEN";
    UNSIGNED8 expr[] = "WHEN";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex61(hT, bag, tag, expr,
                                     nullptr, nullptr, 0x08, 512, &hIdx);
    if (rc != 0)
        FAIL("AdsCreateIndex61 on DATE field returned rc=", (int)rc);
    REQUIRE(hIdx != 0);

    // The build must have indexed every record.
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == 0);
    CHECK(kc == 3);

    // Ascending walk must yield the dates in sorted order.
    const char* want_nomes[3] = {"Alpha", "Beta", "Gamma"};
    const char* want_dates[3] = {"20240115", "20250620", "20261231"};
    REQUIRE(AdsGotoTop(hIdx) == 0);
    for (int i = 0; i < 3; ++i) {
        CHECK(read_field(hT, "NOME").substr(0, 5) == want_nomes[i]);
        UNSIGNED8 dt[16] = {0};
        UNSIGNED16 dl = sizeof(dt);
        UNSIGNED8 fWhen[] = "WHEN";
        REQUIRE(AdsGetDate(hT, fWhen, dt, &dl) == 0);
        CHECK(std::string(reinterpret_cast<char*>(dt), dl) ==
              want_dates[i]);
        if (i < 2) REQUIRE(AdsSkip(hIdx, 1) == 0);
    }

    // SetScope on the date tag: only Beta (2025-06-20) inside.
    REQUIRE(AdsClearScope(hIdx, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hIdx, ADS_BOTTOM) == 0);
    UNSIGNED8 key[] = "20250620";
    REQUIRE(AdsSetScope(hIdx, ADS_TOP, key, 8,
                        ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hIdx, ADS_BOTTOM, key, 8,
                        ADS_STRINGKEY) == 0);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(read_field(hT, "NOME").substr(0, 4) == "Beta");
    UNSIGNED32 kcs = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kcs) == 0);
    CHECK(kcs == 1);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote date tag: GetKeyType=ADS_DATE, DOUBLEKEY scope, seek") {
    // Exact Harbour rddads sequence: AdsGetKeyType decides how OrdScope /
    // DbSeek pack the date (julian double, ADS_DOUBLEKEY). If the remote
    // type is wrong the scope/seek keys never match.
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_date_kt";
    make_date_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "dtidx.dbf";
    UNSIGNED8 alias[] = "DTIDX2";
    REQUIRE(AdsOpenTable(hRC, tname, alias, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);

    UNSIGNED8 bag[]  = "dtidx.cdx";
    UNSIGNED8 tag[]  = "TWHEN";
    UNSIGNED8 expr[] = "WHEN";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0x08, 512, &hIdx) == 0);

    UNSIGNED16 kt = 0;
    REQUIRE(AdsGetKeyType(hIdx, &kt) == 0);
    CHECK(kt == 3);  // ADS_DATE

    // Scope [2026-12-31 .. 2026-12-31] as julian doubles -> Gamma only.
    double dtop = static_cast<double>(jdn(2026, 12, 31));
    double dbot = static_cast<double>(jdn(2026, 12, 31));
    REQUIRE(AdsSetScope(hIdx, ADS_TOP,
                        reinterpret_cast<UNSIGNED8*>(&dtop),
                        sizeof(double), ADS_DOUBLEKEY) == 0);
    REQUIRE(AdsSetScope(hIdx, ADS_BOTTOM,
                        reinterpret_cast<UNSIGNED8*>(&dbot),
                        sizeof(double), ADS_DOUBLEKEY) == 0);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(read_field(hT, "NOME").substr(0, 5) == "Gamma");
    UNSIGNED32 kcs = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kcs) == 0);
    CHECK(kcs == 1);
    REQUIRE(AdsClearScope(hIdx, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hIdx, ADS_BOTTOM) == 0);

    // Seek with a julian double key (rddads DbSeek on a date order).
    double skey = static_cast<double>(jdn(2025, 6, 20));
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIdx, reinterpret_cast<UNSIGNED8*>(&skey),
                    sizeof(double), ADS_DOUBLEKEY,
                    ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    if (found)
        CHECK(read_field(hT, "NOME").substr(0, 4) == "Beta");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("remote NTX date index builds and scopes (legacy AdsCreateIndex)") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_date_ntx";
    make_date_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "dtidx.dbf";
    UNSIGNED8 alias[] = "DTIDX3";
    // ADS_NTX: "INDEX ON ... TO file" RDD path.
    REQUIRE(AdsOpenTable(hRC, tname, alias, ADS_NTX,
                         0, 0, 0, 0, &hT) == 0);

    UNSIGNED8 bag[]  = "dtidx.ntx";
    UNSIGNED8 tag[]  = "";
    UNSIGNED8 expr[] = "WHEN";
    ADSHANDLE hIdx = 0;
    UNSIGNED32 rc = AdsCreateIndex(hT, bag, tag, expr,
                                   nullptr, 0x08, ADS_STRINGKEY, &hIdx);
    if (rc != 0)
        FAIL("remote legacy AdsCreateIndex (NTX) on DATE rc=", (int)rc);

    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hIdx, 0, &kc) == 0);
    CHECK(kc == 3);

    UNSIGNED16 kt = 0;
    REQUIRE(AdsGetKeyType(hIdx, &kt) == 0);
    CHECK(kt == 3);  // ADS_DATE

    double dtop = static_cast<double>(jdn(2025, 6, 20));
    REQUIRE(AdsSetScope(hIdx, ADS_TOP,
                        reinterpret_cast<UNSIGNED8*>(&dtop),
                        sizeof(double), ADS_DOUBLEKEY) == 0);
    REQUIRE(AdsSetScope(hIdx, ADS_BOTTOM,
                        reinterpret_cast<UNSIGNED8*>(&dtop),
                        sizeof(double), ADS_DOUBLEKEY) == 0);
    REQUIRE(AdsGotoTop(hIdx) == 0);
    CHECK(read_field(hT, "NOME").substr(0, 4) == "Beta");

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}
