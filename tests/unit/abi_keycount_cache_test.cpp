// AdsGetKeyCount memoises the live-key count per index (counting it costs one
// deleted_at per index entry, and a TXBrowse asks on every paint). The cache
// is keyed by the table's live generation, so this pins that the generation
// actually moves: a delete, a recall and an append must each be reflected in
// the very next call, on CDX and on ADI alike.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

UNSIGNED32 key_count(ADSHANDLE hIdx) {
    UNSIGNED32 c = 0;
    REQUIRE(AdsGetKeyCount(hIdx, ADS_RESPECTFILTERS, &c) == AE_SUCCESS);
    return c;
}

void run_case(const char* tag, UNSIGNED16 tabletype, const char* idxfile) {
    fs::path tmp = fs::temp_directory_path() / (std::string("oads_kcache_") + tag);
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "kc";
    UNSIGNED8 flddef[] = "Name,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, tabletype, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    UNSIGNED8 fld[] = "Name";
    for (int i = 0; i < 20; ++i) {
        char v[8];
        std::snprintf(v, sizeof(v), "NAME%02d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, fld, (UNSIGNED8*)v,
                             (UNSIGNED32)std::strlen(v)) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }

    UNSIGNED8 file[64]{};
    std::strncpy((char*)file, idxfile, sizeof(file) - 1);
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, file, (UNSIGNED8*)"NAME",
                            (UNSIGNED8*)"Name", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    REQUIRE(AdsShowDeleted(0) == AE_SUCCESS);   // SET DELETED ON

    CHECK(key_count(hIdx) == 20u);
    CHECK(key_count(hIdx) == 20u);              // segunda: sale de la cache

    // Un borrado tiene que verse en la llamada siguiente.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    REQUIRE(AdsDeleteRecord(hTable) == AE_SUCCESS);
    CHECK(key_count(hIdx) == 19u);

    // Y un recall tambien. Con DELETED ON el registro borrado es invisible,
    // asi que hay que destaparlo para poder pararse encima y recuperarlo.
    REQUIRE(AdsShowDeleted(1) == AE_SUCCESS);
    REQUIRE(AdsGotoRecord(hTable, 1) == AE_SUCCESS);
    REQUIRE(AdsRecallRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsShowDeleted(0) == AE_SUCCESS);
    CHECK(key_count(hIdx) == 20u);

    // Un append entra al indice y al conteo.
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    REQUIRE(AdsSetString(hTable, fld, (UNSIGNED8*)"NAME99", 6) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    CHECK(key_count(hIdx) == 21u);

    REQUIRE(AdsShowDeleted(1) == AE_SUCCESS);
    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    { std::error_code ec; fs::remove_all(tmp, ec); }
}

} // namespace

TEST_CASE("AdsGetKeyCount cache: CDX sees delete / recall / append") {
    run_case("cdx", ADS_CDX, "kc.cdx");
}

TEST_CASE("AdsGetKeyCount cache: ADI sees delete / recall / append") {
    run_case("adi", ADS_ADT, "kc.adi");
}
