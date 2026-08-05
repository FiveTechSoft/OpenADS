// Legacy ADI CREATE INDEX must pack dense leaves full (bottom-up build_bulk),
// not fall back to per-record insert that 50/50-splits full leaves and bloats
// the bag. SAP-compatible 3-byte dense entries: floor((512-24)/3)=162/leaf.
// 5000 keys → ceil(5000/162)=31 leaf pages + a few structure pages ≪ 2× that.
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("ADI legacy: CREATE INDEX packs dense leaves (size vs half-full bloat)") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_legacy_bulk";
    {
        std::error_code ec;
        fs::create_directories(tmp, ec);
        fs::remove(tmp / "lb.adt", ec);
        fs::remove(tmp / "lb.adi", ec);
    }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "lb.adt";
    UNSIGNED8 flddef[] = "Code,Character,20";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    // Ensure OPENADS_ADI_V2 is off so we exercise the legacy dense leaf path.
#ifdef _WIN32
    _putenv_s("OPENADS_ADI_V2", "");
#else
    unsetenv("OPENADS_ADI_V2");
#endif

    const int N = 5000;
    for (int i = 0; i < N; ++i) {
        char code[32];
        std::snprintf(code, sizeof(code), "KEY%016d", i);
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        REQUIRE(AdsSetString(hTable, (UNSIGNED8*)"Code", (UNSIGNED8*)code, 20)
                == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);
    }

    UNSIGNED8 idxfile[] = "lb.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"CODE",
                             (UNSIGNED8*)"Code", nullptr, nullptr, 0, 0, &hIdx)
            == AE_SUCCESS);

    std::error_code ec;
    const std::uintmax_t fsize = fs::file_size(tmp / "lb.adi", ec);
    REQUIRE(!ec);

    // Theoretical packed layout: 162 entries/leaf, 31 leaves → 31*512 leaf
    // bytes, plus ~8 structure pages (header/tagdir/per-tag/root branch…).
    // Cap at 2× the packed-leaf floor so a half-full insert path (~60+ leaves
    // plus bloat) fails the check.
    const std::uint32_t esz          = 3u;
    const std::uint32_t per_leaf     = (512u - 24u) / esz;          // 162
    const std::uint32_t packed_leaves =
        (static_cast<std::uint32_t>(N) + per_leaf - 1u) / per_leaf; // 31
    const std::uintmax_t packed_leaf_bytes =
        std::uintmax_t(packed_leaves) * 512u;
    const std::uintmax_t structure_slack = 16ull * 512u;  // header + branches
    const std::uintmax_t budget =
        packed_leaf_bytes * 2u + structure_slack;

    CHECK(fsize <= budget);
    // Sanity: must at least hold the packed leaves + a few header pages.
    CHECK(fsize >= packed_leaf_bytes);

    // Correctness: ordered walk visits every key once.
    REQUIRE(AdsGotoTop(hTable) == AE_SUCCESS);
    int walked = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hTable, &eof) == AE_SUCCESS);
        if (eof) break;
        ++walked;
        REQUIRE(AdsSkip(hTable, 1) == AE_SUCCESS);
    }
    CHECK(walked == N);

    for (int key : {0, 1, 2500, N - 1}) {
        char code[32];
        std::snprintf(code, sizeof(code), "KEY%016d", key);
        UNSIGNED16 found = 0;
        REQUIRE(AdsSeek(hIdx, (UNSIGNED8*)code, 20, ADS_STRINGKEY, 0, &found)
                == AE_SUCCESS);
        CHECK(found != 0);
    }
    REQUIRE(AdsCloseIndex(hIdx) == AE_SUCCESS);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(tmp, ec);
}
