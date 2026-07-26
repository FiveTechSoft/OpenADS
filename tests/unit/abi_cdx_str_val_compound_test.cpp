// A CHARACTER key that merely CONTAINS Val() must not be treated as a numeric
// key. The rddads/Clipper idiom
//
//     INDEX ON cDocumeTra + STR(VAL(cConIntTra), 3, 0) TAG ...
//
// concatenates text: STR() returns characters, so the key is character data of
// width 8 + 3. Classifying the tag as numeric because the expression contains
// "VAL(" makes the build loop demand a number from the concatenation and abort
// with ADSCDX/5000 "failed to evaluate numeric index expression" partway
// through the table (seen on a Harbour/FiveWin ERP's monthly movement tables).
//
// The companion case — a WHOLE Val() call — must stay numeric (issue #130,
// covered by abi_cdx_multitag_create_test).
#include "doctest.h"
#include "openads/ace.h"

#include "drivers/cdx/cdx_index.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void set_c(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[32]{};
    std::strncpy(reinterpret_cast<char*>(f), field, 31);
    UNSIGNED8 v[32]{};
    std::strncpy(reinterpret_cast<char*>(v), val, 31);
    REQUIRE(AdsSetString(h, f, v,
            static_cast<UNSIGNED32>(std::strlen(val))) == AE_SUCCESS);
}

UNSIGNED32 recno(ADSHANDLE h) {
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(h, 0, &rn) == AE_SUCCESS);
    return rn;
}

}  // namespace

TEST_CASE("CDX: a character key containing STR(VAL(...)) is not a numeric key") {
    auto dir = fs::temp_directory_path() / "openads_cdx_str_val_compound";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    const std::string dir_str = dir.string();
    std::vector<UNSIGNED8> srv(dir_str.begin(), dir_str.end());
    srv.push_back(0);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == AE_SUCCESS);

    UNSIGNED8 def[]   = "CDOCUME,C,8,0;CCONINT,C,3,0";
    UNSIGNED8 tname[] = "mvtos";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == AE_SUCCESS);

    // Sorted by CDOCUME + STR(VAL(CCONINT),3,0):
    //   "00000010" + "  3"  -> rec3
    //   "00000010" + " 12"  -> rec2
    //   "00000020" + "  7"  -> rec1
    struct Row { const char* doc; const char* num; };
    const Row rows[3] = {
        {"00000020", "7"},    // rec1
        {"00000010", "12"},   // rec2
        {"00000010", "3"},    // rec3
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        set_c(hT, "CDOCUME", r.doc);
        set_c(hT, "CCONINT", r.num);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }

    UNSIGNED8 idxfile[] = "mvtos";
    UNSIGNED8 tag[]     = "BYDOC";
    UNSIGNED8 expr[]    = "CDOCUME+STR(VAL(CCONINT),3,0)";
    ADSHANDLE hI = 0;
    // Before the fix this returned 5000: the tag was marked numeric and the
    // build loop could not evaluate the concatenation as a number.
    REQUIRE(AdsCreateIndex61(hT, idxfile, tag, expr, nullptr, nullptr, 0, 0, &hI)
            == AE_SUCCESS);

    REQUIRE(AdsSetIndexOrderByHandle(hT, hI) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 3u);
    REQUIRE(AdsSkip(hT, 1) == AE_SUCCESS);
    CHECK(recno(hT) == 2u);
    REQUIRE(AdsSkip(hT, 1) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);

    REQUIRE(AdsCloseIndex(hI) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // The stored key is character data 8 + 3 wide, not an 8-byte Fox numeric.
    {
        std::string cdxpath = (dir / "mvtos.cdx").string();
        openads::drivers::cdx::CdxIndex idx;
        auto r = idx.open_named(cdxpath,
                                openads::drivers::IndexOpenMode::ReadOnly,
                                "BYDOC");
        REQUIRE(r.has_value());
        CHECK(idx.key_length() == 11);
    }

    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
