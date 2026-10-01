#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("LOCAL ADT exclusive bulk append does not accumulate record locks") {
    const fs::path dir = fs::temp_directory_path() /
                         "openads_adt_append_exclusive_regression";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    UNSIGNED8 srv[260]{};
    const std::string root = dir.string();
    REQUIRE(root.size() < sizeof(srv));
    std::memcpy(srv, root.c_str(), root.size());
    ADSHANDLE conn = 0, table = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                         &conn) == AE_SUCCESS);
    UNSIGNED8 name[] = "bulk.adt";
    UNSIGNED8 fields[] = "NAME,Character,30;AGE,Numeric,5";
    REQUIRE(AdsCreateTable(conn, name, nullptr, ADS_ADT, ADS_ANSI,
                           0, 0, 0, fields, &table) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(table) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_ADT, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_EXCLUSIVE, &table) == AE_SUCCESS);
    REQUIRE(AdsSetDeferredFlush(table, 1) == AE_SUCCESS);
    UNSIGNED8 field[] = "NAME";
    UNSIGNED8 value[] = "written";
    for (int i = 1; i <= 10000; ++i) {
        REQUIRE(AdsAppendRecord(table) == AE_SUCCESS);
        REQUIRE(AdsSetString(table, field, value, 7) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(table) == AE_SUCCESS);
        if (i == 1 || i == 5000 || i == 10000) {
            UNSIGNED16 nlocks = 999;
            REQUIRE(AdsGetNumLocks(table, &nlocks) == AE_SUCCESS);
            CHECK(nlocks == 0);
        }
    }
    REQUIRE(AdsCloseTable(table) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_ADT, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_READONLY, &table) == AE_SUCCESS);
    UNSIGNED32 count = 0;
    REQUIRE(AdsGetRecordCount(table, ADS_RESPECTFILTERS, &count) == AE_SUCCESS);
    CHECK(count == 10000u);
    REQUIRE(AdsGotoRecord(table, 10000) == AE_SUCCESS);
    UNSIGNED8 out[64]{};
    UNSIGNED32 len = sizeof(out);
    REQUIRE(AdsGetString(table, field, out, &len, 0) == AE_SUCCESS);
    CHECK(std::string(reinterpret_cast<char*>(out), len) == "written");
    REQUIRE(AdsCloseTable(table) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(conn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
