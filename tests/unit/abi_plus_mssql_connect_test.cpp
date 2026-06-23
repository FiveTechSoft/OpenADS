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
