#include "doctest.h"
#include "drivers/dbf_common.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace openads::drivers;

namespace {
std::string enc(DbfFieldType t, std::uint16_t len, std::uint8_t dec,
                const std::string& v) {
    DbfField f;
    f.type = t;
    f.length = len;
    f.decimals = dec;
    f.record_offset = 1;
    std::vector<std::uint8_t> rec(1u + len, 'x');
    auto r = encode_field_string(f, rec.data(), rec.size(), v);
    REQUIRE(static_cast<bool>(r));
    return std::string(rec.begin() + 1, rec.end());
}
}

TEST_CASE("N field text is right-aligned with declared decimals") {
    CHECK(enc(DbfFieldType::Numeric, 8, 2, "12.5") == "   12.50");
    CHECK(enc(DbfFieldType::Numeric, 8, 2, "-3") == "   -3.00");
    CHECK(enc(DbfFieldType::Numeric, 6, 0, "42") == "    42");
    CHECK(enc(DbfFieldType::Numeric, 6, 0, "7.6") == "     8");
    CHECK(enc(DbfFieldType::Numeric, 8, 2, "-0.001") == "    0.00");
    CHECK(enc(DbfFieldType::Float, 8, 3, "1.5") == "   1.500");
}

TEST_CASE("N field text that is not a number or is too wide keeps the old copy") {
    CHECK(enc(DbfFieldType::Numeric, 5, 2, "abc") == "abc  ");
    CHECK(enc(DbfFieldType::Numeric, 4, 2, "12345.67") == "1234");
}

TEST_CASE("N field text write speed (informational)") {
    DbfField f;
    f.type = DbfFieldType::Numeric; f.length = 12; f.decimals = 2; f.record_offset = 1;
    std::vector<std::uint8_t> rec(13, ' ');
    const int N = 200000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
        (void)encode_field_string(f, rec.data(), rec.size(), "12345.67");
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - t0).count() / N;
    MESSAGE("N-field encode ns per call: " << ns);
    CHECK(ns < 20000);
}
