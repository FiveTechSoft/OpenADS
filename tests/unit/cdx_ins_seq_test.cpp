// Focused repro: B_BIG's exact INS sequence through CdxIndex with
// 8-byte big-endian order-preserving keys (same mechanics as the
// FoxNumeric numeric tag). Cross-leaf order must hold.
#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::cdx::CdxIndex;

namespace {
std::string enc8(std::uint32_t v) {
    std::string s(8, '\0');
    for (int i = 0; i < 4; ++i) s[4 + i] = static_cast<char>((v >> (24 - i * 8)) & 0xFF);
    s[0] = static_cast<char>(0xC0);  // positive-exponent flavour, fixed prefix
    return s;
}
std::uint16_t rd16(const std::uint8_t* p) { return p[0] | (p[1] << 8); }
std::uint32_t rd32le(const std::uint8_t* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
} // namespace

TEST_CASE("CDX numeric-style duplicate keys stay ordered across splits (INS repro)") {
    auto p = fs::temp_directory_path() / "openads_ins_seq.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "IDX03", "INS", 8,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (std::uint32_t i = 0; i < 300; ++i) {
            const std::uint32_t ins = 1000 + (i % 90) * 37;
            REQUIRE(ix.insert(i + 1, enc8(ins)).has_value());
        }
        REQUIRE(ix.flush().has_value());

        // Walk the whole tag through the public cursor API.
        auto first = ix.seek_first();
        REQUIRE(first.has_value());
        REQUIRE(first.value().positioned);
        std::string prev_key;
        std::uint32_t prev_rec = 0;
        int n = 0;
        auto cur = first;
        while (cur.has_value() && cur.value().positioned) {
            const std::string k = ix.current_key();
            const std::uint32_t r = cur.value().recno;
            if (n > 0) {
                INFO("prev rec=", prev_rec, " cur rec=", r, " at walk#", n);
                CHECK((k > prev_key || (k == prev_key && r > prev_rec)));
            }
            prev_key = k;
            prev_rec = r;
            ++n;
            cur = ix.next();
        }
        CHECK(n == 300);
    }
    fs::remove(p);
}
