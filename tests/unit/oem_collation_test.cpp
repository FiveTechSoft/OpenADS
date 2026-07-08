#include "doctest.h"
#include "fixtures/polish_oem_fixture.h"
#include "engine/oem_collation.h"

#include <cstring>

using openads::engine::compare_oem_keys;
using openads::engine::lookup_oem_collation;

TEST_CASE("OEM collation lookup accepts PL852 and NTXPL852 aliases") {
    const auto* pl = lookup_oem_collation("PL852");
    const auto* ntx = lookup_oem_collation("NTXPL852");
    const auto* mix = lookup_oem_collation("ntxpl852");
    REQUIRE(pl != nullptr);
    REQUIRE(ntx != nullptr);
    REQUIRE(mix != nullptr);
    CHECK(pl == ntx);
    CHECK(pl == mix);
    CHECK(lookup_oem_collation("BINARY") == nullptr);
    CHECK(lookup_oem_collation("MAZOVIA") == nullptr);
}

TEST_CASE("PL852 sort weights place L-stroke between L and M") {
    const auto* c = lookup_oem_collation("PL852");
    REQUIRE(c != nullptr);
    const auto& s = c->sort;
    CHECK(s[static_cast<unsigned char>('L')] < s[0x9D]);
    CHECK(s[0x9D] < s[static_cast<unsigned char>('M')]);
    CHECK(s[static_cast<unsigned char>('Z')] > s[0x9D]);
}

TEST_CASE("compare_oem_keys honours collation over raw memcmp") {
    const auto* c = lookup_oem_collation("NTXPL852");
    REQUIRE(c != nullptr);
    const char* lac = "LAC";
    const char* lab = openads::test::kPolishLab3;
    const char* mad = "MAD";
    // Binary: 0x9D (157) sorts after 'Z' (90).
    CHECK(std::memcmp(lab, mad, 1) > 0);
    // Collation: Ł sorts before M.
    CHECK(compare_oem_keys(c->sort, lab, mad, 1) < 0);
    CHECK(compare_oem_keys(c->sort, lac, lab, 1) < 0);
    CHECK(compare_oem_keys(c->sort, mad, lab, 1) > 0);
    CHECK(compare_oem_keys(nullptr, lab, mad, 1) > 0);
}

TEST_CASE("compare_oem_keys prefix length matches soft seek semantics") {
    const auto* c = lookup_oem_collation("PL852");
    REQUIRE(c != nullptr);
    const char a[] = "MADXYZ  ";
    const char b[] = "MZZZZZ  ";
    CHECK(compare_oem_keys(c->sort, a, b, 1) == 0);
    CHECK(compare_oem_keys(c->sort, a, b, 3) < 0);
}

TEST_CASE("lookup_oem_collation rejects null and empty names") {
    CHECK(lookup_oem_collation(nullptr) == nullptr);
    CHECK(lookup_oem_collation("") == nullptr);
}

TEST_CASE("compare_oem_keys zero length always ties") {
    const auto* c = lookup_oem_collation("NTXPL852");
    REQUIRE(c != nullptr);
    const char a[] = "Z";
    const char b[] = "\x9D";
    CHECK(compare_oem_keys(c->sort, a, b, 0) == 0);
    CHECK(compare_oem_keys(nullptr, a, b, 0) == 0);
}

TEST_CASE("compare_oem_keys tie-breaks equal weights by raw byte") {
    const auto* c = lookup_oem_collation("PL852");
    REQUIRE(c != nullptr);
    // Upper/lower pairs share collation weight in PL852 table.
    CHECK(c->sort[static_cast<unsigned char>('A')] ==
          c->sort[static_cast<unsigned char>('a')]);
    CHECK(compare_oem_keys(c->sort, "A", "a", 1) < 0);
    CHECK(compare_oem_keys(c->sort, "a", "A", 1) > 0);
}