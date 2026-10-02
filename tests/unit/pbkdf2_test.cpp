#include "doctest.h"

#include "engine/pbkdf2.h"
#include "engine/sha256.h"

#include <array>
#include <cstring>

TEST_CASE("SHA-256 empty string matches RFC 6234 vector") {
    const auto d = openads::engine::sha256("");
    static const std::uint8_t expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    CHECK(std::memcmp(d.data(), expected, 32) == 0);
}

TEST_CASE("PBKDF2 derives 32-byte key and differs from legacy zero-pad") {
    const auto legacy = openads::engine::derive_legacy_encryption_key("swordfish");
    const auto strong = openads::engine::derive_encryption_key("swordfish");
    CHECK(legacy != strong);
    CHECK(strong.size() == 32);
}
TEST_CASE("salted credentials verify without accepting damaged hashes") {
    const auto a = openads::engine::hash_password("secret");
    const auto b = openads::engine::hash_password("secret");
    CHECK(a != b);
    CHECK(openads::engine::password_is_hash(a));
    CHECK(openads::engine::verify_password(a, "secret"));
    CHECK_FALSE(openads::engine::verify_password(a, "wrong"));
    CHECK_FALSE(openads::engine::verify_password(a.substr(0, 40), "secret"));
    CHECK(openads::engine::verify_password("legacy", "legacy"));
    CHECK_FALSE(openads::engine::verify_password("legacy", "legacy", false));
    CHECK_FALSE(openads::engine::verify_password("$openads-bad", "$openads-bad"));
}
