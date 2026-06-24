#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include <cstring>

#if !defined(OPENADS_WITH_MSSQL)
TEST_CASE("ABI: mssql backend disabled at compile time") {
    UNSIGNED8 uri[] = "mssql://u:p@127.0.0.1:1433/db";
    ADSHANDLE hConn = 0;
    const UNSIGNED32 rc = AdsConnect60(uri, ADS_LOCAL_SERVER,
                                       nullptr, nullptr, 0, &hConn);
    CHECK(rc == openads::AE_FUNCTION_NOT_AVAILABLE);
}
#endif

#if defined(OPENADS_WITH_MSSQL)
#include <cstdlib>
#include <string>
#include <vector>

namespace {
const char* mssql_connstr() {
    const char* v = std::getenv("OPENADS_TEST_MSSQL_CONNSTR");
    return (v && v[0]) ? v : nullptr;
}
}
TEST_CASE("ABI: mssql native connect authenticates over TCP") {
    const char* cs = mssql_connstr();
    if (!cs) { MESSAGE("OPENADS_TEST_MSSQL_CONNSTR not set; skipping"); return; }
    std::vector<UNSIGNED8> srv(std::strlen(cs)+1);
    std::memcpy(srv.data(), cs, std::strlen(cs)+1);
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    CHECK(hConn != 0);
    CHECK(AdsDisconnect(hConn) == 0);
}
TEST_CASE("ABI: mssql native connect rejects a wrong password") {
    const char* cs = mssql_connstr();
    if (!cs) { MESSAGE("skipping"); return; }
    // Corrupt the password segment: replace ':<pass>@' — simplest is to append
    // a bogus suffix to the password via a hand-built bad connstr from parts.
    std::string bad = std::string(cs);
    auto at = bad.find('@'); auto col = bad.rfind(':', at);
    REQUIRE(at != std::string::npos); REQUIRE(col != std::string::npos);
    bad.insert(at, "WRONG");           // corrupt the password
    std::vector<UNSIGNED8> srv(bad.size()+1);
    std::memcpy(srv.data(), bad.c_str(), bad.size()+1);
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srv.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) != 0);
}
#endif
