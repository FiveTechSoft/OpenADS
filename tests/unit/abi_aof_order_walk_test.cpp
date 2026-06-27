// Regression: an ordered walk with an active AOF/filter must follow the
// ACTIVE INDEX order, not recno order. The qa-diff harness (rddads vs native
// DBFCDX) flagged "filter AGE>=30" walking out of order under OpenADS; this
// confirms it at the ABI (no rddads) so it is pinned as an engine fix.
// Root cause: install_aof_bitmap built the sparse recno_sequence_ in recno
// order, which goto_top/skip prefer over the active index order.
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
fs::path stage(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    const std::uint16_t rec_len = 1 + 10 + 3;
    const std::uint16_t hdr_len = 32 + 32 + 32 + 1;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03; hdr[4] = 6;
    hdr[8] = hdr_len & 0xFF; hdr[9] = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> f1{};
    std::strncpy(reinterpret_cast<char*>(f1.data()), "NAME", 11);
    f1[11] = 'C'; f1[16] = 10;
    push(f1.data(), f1.size());
    std::array<std::uint8_t, 32> f2{};
    std::strncpy(reinterpret_cast<char*>(f2.data()), "AGE", 11);
    f2[11] = 'N'; f2[16] = 3; f2[17] = 0;
    push(f2.data(), f2.size());
    file.push_back(0x0D);
    auto rec = [&](const char* nm, int age) {
        file.push_back(' ');
        std::string n = nm; n.resize(10, ' '); push(n.data(), n.size());
        char a[4]; std::snprintf(a, sizeof(a), "%3d", age); push(a, 3);
    };
    rec("r1", 40); rec("r2", 30); rec("r3", 45);
    rec("r4", 28); rec("r5", 35); rec("r6", 31);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}
int age_now(ADSHANDLE hT) {
    UNSIGNED8 fld[8]; std::memcpy(fld, "AGE", 4);
    UNSIGNED8 buf[16] = {0}; UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hT, fld, buf, &cap, 0) == 0);
    return std::atoi(reinterpret_cast<char*>(buf));
}
std::vector<int> walk_ages(ADSHANDLE hT) {
    std::vector<int> got;
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    while (eof == 0 && got.size() < 50) {
        got.push_back(age_now(hT));
        REQUIRE(AdsSkip(hT, 1) == 0);
        REQUIRE(AdsAtEOF(hT, &eof) == 0);
    }
    return got;
}
} // namespace

TEST_CASE("ABI: ordered walk under an active AOF keeps the index order") {
    auto dir = fs::temp_directory_path() / "openads_aof_order_walk";
    std::error_code ec; fs::remove_all(dir, ec);
    stage(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hT = 0;
    UNSIGNED8 nm[16] = "data";
    REQUIRE(AdsOpenTable(hConn, nm, nm, ADS_CDX, 1, 1, 0, 1, &hT) == 0);

    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, (UNSIGNED8*)"data", (UNSIGNED8*)"TAGE",
                            (UNSIGNED8*)"AGE", nullptr, nullptr, 0, 512, &hIdx) == 0);

    // Filter AGE>=30 (28 fails). With TAGE active the walk must be ascending
    // by AGE, exactly like native DBFCDX.
    std::string cond = "AGE >= 30";
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)cond.data(), 0) == 0);
    CHECK(walk_ages(hT) == std::vector<int>{30, 31, 35, 40, 45});

    // Re-applying the AOF must also stay ordered.
    REQUIRE(AdsClearAOF(hT) == 0);
    REQUIRE(AdsSetAOF(hT, (UNSIGNED8*)cond.data(), 0) == 0);
    CHECK(walk_ages(hT) == std::vector<int>{30, 31, 35, 40, 45});

    // Clearing the AOF restores the full table in index order.
    REQUIRE(AdsClearAOF(hT) == 0);
    CHECK(walk_ages(hT) == std::vector<int>{28, 30, 31, 35, 40, 45});

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
