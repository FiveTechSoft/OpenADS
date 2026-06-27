// CDX order ordinals (OrdSetFocus(N) / SET ORDER TO <n> / OrdNumber) must
// follow TAG CREATION order, exactly as ADS / rddads expose them — NOT the
// alphabetical order of the tag names. A compound CDX stores its tags in a
// name-keyed "tag of tags" B-tree, so enumerating that tree yields tag names
// alphabetically; using that as the ordinal sequence makes SET ORDER TO 2
// select a different tag than the one the migrating app created second. This
// is the #1 migration breaker for apps that select orders by number.
//
// Repro: create tag "TZ" first, then "TA". Creation order is [TZ, TA];
// alphabetical is [TA, TZ]. Ordinal 1 must resolve to TZ (created first).
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    const std::uint16_t rec_len = 1 + 10 + 5;
    const std::uint16_t hdr_len = 32 + 32 + 32 + 1;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03; hdr[4] = 2;
    hdr[8] = hdr_len & 0xFF; hdr[9] = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> f1{};
    std::strncpy(reinterpret_cast<char*>(f1.data()), "NOME", 11);
    f1[11] = 'C'; f1[16] = 10;
    push(f1.data(), f1.size());
    std::array<std::uint8_t, 32> f2{};
    std::strncpy(reinterpret_cast<char*>(f2.data()), "CODE", 11);
    f2[11] = 'C'; f2[16] = 5;
    push(f2.data(), f2.size());
    file.push_back(0x0D);
    auto rec = [&](const std::string& nome, const std::string& code) {
        file.push_back(' ');
        std::string n = nome; n.resize(10, ' '); push(n.data(), n.size());
        std::string c = code; c.resize(5, ' ');  push(c.data(), c.size());
    };
    rec("bravo", "002");
    rec("alpha", "001");
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::string idx_expr(ADSHANDLE hIdx) {
    UNSIGNED8 buf[128] = {0};
    UNSIGNED16 cap = sizeof(buf);
    REQUIRE(AdsGetIndexExpr(hIdx, buf, &cap) == 0);
    return std::string(reinterpret_cast<const char*>(buf));
}

std::string idx_name(ADSHANDLE hIdx) {
    UNSIGNED8 buf[64] = {0};
    UNSIGNED16 cap = sizeof(buf);
    REQUIRE(AdsGetIndexName(hIdx, buf, &cap) == 0);
    return std::string(reinterpret_cast<const char*>(buf));
}

} // namespace

TEST_CASE("CDX ordinal sequence follows tag CREATION order, not alphabetical") {
    auto dir = fs::temp_directory_path() / "openads_cdx_ordinal";
    std::error_code ec; fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 nm[16] = "data";
    REQUIRE(AdsOpenTable(hConn, nm, nm, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[16] = "data";
    ADSHANDLE hZ = 0, hA = 0;
    // Create TZ first (key UPPER(NOME)), then TA (key CODE).
    REQUIRE(AdsCreateIndex61(hTable, fn, (UNSIGNED8*)"TZ", (UNSIGNED8*)"UPPER(NOME)",
                            nullptr, nullptr, 0, 512, &hZ) == 0);
    REQUIRE(AdsCreateIndex61(hTable, fn, (UNSIGNED8*)"TA", (UNSIGNED8*)"CODE",
                            nullptr, nullptr, 0, 512, &hA) == 0);

    // Ordinal 1 = first-created tag (TZ / UPPER(NOME)); ordinal 2 = TA / CODE.
    ADSHANDLE h1 = 0, h2 = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &h1) == 0);
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &h2) == 0);

    CHECK(idx_name(h1) == "TZ");
    CHECK(idx_expr(h1) == "UPPER(NOME)");
    CHECK(idx_name(h2) == "TA");
    CHECK(idx_expr(h2) == "CODE");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
