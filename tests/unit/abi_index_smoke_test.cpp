#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 3;
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 4;       hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    auto push = [&](const char* k){
        file.push_back(' ');
        for (int i = 0; i < 4; ++i)
            file.push_back(i < (int)std::strlen(k) ? static_cast<std::uint8_t>(k[i]) : ' ');
    };
    push("CCCC");  // recno 1
    push("AAAA");  // recno 2
    push("BBBB");  // recno 3
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("ABI index smoke: create NTX, seek, walk in order, scope") {
    const auto dir = fs::temp_directory_path() / "openads_m3_abi_idx";
    std::error_code ec;
    fs::remove_all(dir, ec);
    make_dbf(dir, "data.dbf");

    ADSHANDLE hConn = 0;
    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 leaf[64] = "data.dbf";
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    auto idx_path = (dir / "data.ntx").string();
    UNSIGNED8 idx_buf[260];
    std::memcpy(idx_buf, idx_path.c_str(), idx_path.size() + 1);

    UNSIGNED8 tag[64] = "T1";
    UNSIGNED8 expr[64] = "TAG";
    ADSHANDLE hIndex = 0;
    REQUIRE(AdsCreateIndex(hTable, idx_buf, tag, expr, nullptr, 0, 0, &hIndex)
            == 0);

    // Walk: should now see records in TAG-sorted order.
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 recno = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 2);  // AAAA

    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);  // BBBB

    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 1);  // CCCC

    // Seek by key.
    UNSIGNED8 key[8] = "BBBB";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(hIndex, key, 4 /*key_len*/, 0 /*key_type*/,
                    0 /*seek_type=hard*/, &found) == 0);
    CHECK(found == 1);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);

    REQUIRE(AdsSetIndexOrderByHandle(hTable, hIndex) == 0);
    // Scope: top = BBBB, bottom = CCCC -> only recno 3 then recno 1.
    UNSIGNED8 stop[8] = "BBBB";
    UNSIGNED8 sbot[8] = "CCCC";
    REQUIRE(AdsSetScope(hIndex, ADS_TOP,    stop, 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hIndex, ADS_BOTTOM, sbot, 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 3);
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &recno) == 0);
    CHECK(recno == 1);
    REQUIRE(AdsSkip(hTable, 1) == 0);
    UNSIGNED16 at_eof = 0;
    REQUIRE(AdsAtEOF(hTable, &at_eof) == 0);
    CHECK(at_eof == 1);

    REQUIRE(AdsClearScope(hIndex, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hIndex, ADS_BOTTOM) == 0);

    // Pack/Zap (M9.8 real impls): must succeed.
    CHECK(AdsPackTable(hTable) == 0);
    CHECK(AdsZapTable (hTable) == 0);

    // M-AOF.3 — AdsSetAOF now actually parses + evaluates the cond
    // string. Use a real comparison expression here; the old "true"
    // stub-friendly literal would no longer parse since bare bool
    // literals aren't a valid AOF leaf in V1. OptLevel still reports
    // NONE because the V1 path is a full table scan (M-AOF.4 will
    // start reporting PART / FULL once leaves route through index
    // range scans).
    UNSIGNED8 cond[64] = "NAME = 'X'";
    REQUIRE(AdsSetAOF(hTable, cond, 1) == 0);
    UNSIGNED16 lvl = 0;
    REQUIRE(AdsGetAOFOptLevel(hTable, &lvl, nullptr, nullptr) == 0);
    CHECK(lvl == ADS_OPTIMIZED_NONE);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    fs::remove_all(dir, ec);
}
