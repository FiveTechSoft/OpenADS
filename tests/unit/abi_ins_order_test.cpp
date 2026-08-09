// Bisect repro for the numeric-tag cross-leaf recno inversion seen in
// sizecmp (T_ADS.cdx IDX03: 2110@211 stored before 2110@121).
// Replicates the exact RDDADS/ACE append flow: blank-append auto-inserts
// a blank key, then each field REPLACE erases/inserts keys at commit.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
constexpr const char* kNames[10] = {"Alice", "Bob", "Phillip", "Charlie",
                                    "Linda", "Finland", "Diana", "Lucy",
                                    "Jony", "Edward"};
constexpr const char* kCities[10] = {"Madrid", "Barcelona", "Panipat",
                                     "Valencia", "Iris", "Dallas", "Sevilla",
                                     "Sevilla", "Walker", "Bilbao"};
} // namespace

TEST_CASE("ABI sequential appends keep numeric tag order (INS bisect)") {
    const auto dir = fs::temp_directory_path() / "openads_ins_bisect";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string d = dir.string();

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60((UNSIGNED8*)d.data(), ADS_LOCAL_SERVER, nullptr,
                         nullptr, 0, &hConn) == 0);
    ADSHANDLE hTbl = 0, hIdx = 0;
    REQUIRE(AdsCreateTable(hConn, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                           ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                           ADS_CHECKRIGHTS, 0,
                           (UNSIGNED8*)"NAME,C,19;CITY,C,15;INS,N,4",
                           &hTbl) == 0);
    const char* tags[3][2] = {{"IDX01", "NAME"}, {"IDX02", "CITY"},
                              {"IDX03", "INS"}};
    for (auto& t : tags) {
        REQUIRE(AdsCreateIndex61(hTbl, (UNSIGNED8*)"mt.cdx",
                                 (UNSIGNED8*)t[0], (UNSIGNED8*)t[1],
                                 nullptr, nullptr, 0, 0, &hIdx) == 0);
    }
    REQUIRE(AdsCloseTable(hTbl) == 0);

    REQUIRE(AdsOpenTable(hConn, (UNSIGNED8*)"mt.dbf", nullptr, ADS_CDX,
                         ADS_ANSI, ADS_PROPRIETARY_LOCKING, ADS_CHECKRIGHTS,
                         ADS_SHARED, &hTbl) == 0);
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"mt.cdx", &hIdx, nullptr) == 0);
    // sizecmp/B_BIG flow: natural order active, commit+unlock per record.
    REQUIRE(AdsSetIndexOrder(hTbl, (UNSIGNED8*)"") == 0);

    for (int i = 0; i < 300; ++i) {
        char ins[16];
        std::snprintf(ins, sizeof(ins), "%u", 1000u + (i % 90) * 37u);
        REQUIRE(AdsAppendRecord(hTbl) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"NAME",
                             (UNSIGNED8*)kNames[i % 10],
                             (UNSIGNED16)std::strlen(kNames[i % 10])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"CITY",
                             (UNSIGNED8*)kCities[i % 10],
                             (UNSIGNED16)std::strlen(kCities[i % 10])) == 0);
        REQUIRE(AdsSetString(hTbl, (UNSIGNED8*)"INS", (UNSIGNED8*)ins,
                             (UNSIGNED16)std::strlen(ins)) == 0);
        REQUIRE(AdsWriteRecord(hTbl) == 0);
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &rec) == 0);
        REQUIRE(AdsUnlockRecord(hTbl, rec) == 0);
    }

    // Ordered walk on the numeric tag: (INS, recno) nondecreasing.
    ADSHANDLE idxs[8] = {0};
    UNSIGNED16 nidx = 8;
    REQUIRE(AdsOpenIndex(hTbl, (UNSIGNED8*)"mt.cdx", idxs, &nidx) == 0);
    REQUIRE(nidx == 3);
    REQUIRE(AdsSetIndexOrderByHandle(hTbl, idxs[2]) == 0);
    REQUIRE(AdsGotoTop(hTbl) == 0);
    double prev_ins = -1;
    UNSIGNED32 prev_rec = 0;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTbl, &eof) == 0);
        if (eof) break;
        double v = 0;
        REQUIRE(AdsGetDouble(hTbl, (UNSIGNED8*)"INS", &v) == 0);
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTbl, 0, &rec) == 0);
        if (n > 0) {
            INFO("prev ins=", prev_ins, "@", prev_rec, " cur ins=", v, "@",
                 rec, " walk#", n);
            CHECK((v > prev_ins || (v == prev_ins && rec > prev_rec)));
        }
        prev_ins = v;
        prev_rec = rec;
        ++n;
        REQUIRE(AdsSkip(hTbl, 1) == 0);
    }
    CHECK(n == 300);

    REQUIRE(AdsCloseTable(hTbl) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    // keep the dir for the python leaf decoder on failure
}
