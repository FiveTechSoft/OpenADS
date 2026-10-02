#include "engine/pbkdf2.h"

#include "engine/sha256.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <fstream>
#include <stdexcept>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace openads::engine {

namespace {

constexpr std::string_view kOpenAdsSalt = "OpenADS-Enc-v1";
constexpr std::uint32_t    kIterations  = 100000u;

}  // namespace

std::array<std::uint8_t, 32>
pbkdf2_sha256(std::string_view password,
              std::string_view salt,
              std::uint32_t iterations) {
    std::array<std::uint8_t, 32> out{};
    if (iterations == 0) return out;

    const std::size_t blocks =
        (out.size() + Sha256Digest{}.size() - 1) / Sha256Digest{}.size();
    std::size_t produced = 0;

    for (std::size_t block = 1; block <= blocks; ++block) {
        const std::uint32_t block_index = static_cast<std::uint32_t>(block);
        std::uint8_t be[4] = {
            static_cast<std::uint8_t>((block_index >> 24) & 0xFFu),
            static_cast<std::uint8_t>((block_index >> 16) & 0xFFu),
            static_cast<std::uint8_t>((block_index >> 8) & 0xFFu),
            static_cast<std::uint8_t>(block_index & 0xFFu),
        };

        std::string u_input;
        u_input.reserve(salt.size() + 4);
        u_input.append(salt.data(), salt.size());
        u_input.append(reinterpret_cast<const char*>(be), 4);

        Sha256Digest u = hmac_sha256(password, u_input);
        Sha256Digest t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = hmac_sha256(password,
                            std::string_view(
                                reinterpret_cast<const char*>(u.data()),
                                u.size()));
            for (std::size_t j = 0; j < t.size(); ++j) {
                t[j] ^= u[j];
            }
        }

        const std::size_t copy =
            std::min<std::size_t>(t.size(), out.size() - produced);
        std::memcpy(out.data() + produced, t.data(), copy);
        produced += copy;
    }
    return out;
}

std::array<std::uint8_t, 32>
derive_legacy_encryption_key(std::string_view password) {
    std::array<std::uint8_t, 32> key{};
    const std::size_t n =
        std::min<std::size_t>(password.size(), key.size());
    if (n > 0) {
        std::memcpy(key.data(), password.data(), n);
    }
    return key;
}

std::array<std::uint8_t, 32>
derive_encryption_key(std::string_view password) {
    return pbkdf2_sha256(password, kOpenAdsSalt, kIterations);
}


namespace {
constexpr std::string_view kPasswordPrefix = "$openads-pbkdf2-sha256$100000$";
std::string hex_bytes(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (char ch : bytes) {
        const auto c = static_cast<unsigned char>(ch);
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}
bool decode_hex(std::string_view hex, std::string& out) {
    if (hex.size() % 2 != 0) return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    out.clear();
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int a = digit(hex[i]), b = digit(hex[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(static_cast<char>((a << 4) | b));
    }
    return true;
}
bool constant_equal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    volatile unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        diff = diff | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    return diff == 0;
}
}
bool password_is_hash(std::string_view stored) {
    return stored.substr(0, kPasswordPrefix.size()) == kPasswordPrefix;
}
std::string hash_password(std::string_view password) {
    std::string salt(16, '\0');
#if defined(_WIN32)
    // Use the OS CSPRNG, never a predictable PRNG or fixed fallback salt.
    HMODULE lib = LoadLibraryW(L"bcrypt.dll");
    if (!lib) throw std::runtime_error("password salt RNG unavailable");
    using RandomFn = LONG (WINAPI*)(void*, unsigned char*, ULONG, ULONG);
    RandomFn rng = nullptr;
    const auto symbol = GetProcAddress(lib, "BCryptGenRandom");
    static_assert(sizeof(rng) == sizeof(symbol), "function pointer size");
    std::memcpy(&rng, &symbol, sizeof(rng));
    const bool ok = rng && rng(nullptr, reinterpret_cast<unsigned char*>(salt.data()),
                               static_cast<ULONG>(salt.size()), 2) >= 0;
    FreeLibrary(lib);
    if (!ok) throw std::runtime_error("password salt RNG failed");
#else
    std::ifstream rng("/dev/urandom", std::ios::binary);
    if (!rng.read(salt.data(), static_cast<std::streamsize>(salt.size())))
        throw std::runtime_error("password salt RNG failed");
#endif
    const auto key = pbkdf2_sha256(password, salt, 100000);
    const std::string_view bytes(reinterpret_cast<const char*>(key.data()), key.size());
    return std::string(kPasswordPrefix) + hex_bytes(salt) + "$" + hex_bytes(bytes);
}
bool verify_password(std::string_view stored, std::string_view password, bool allow_legacy) {
    // Never interpret a damaged hash as a plaintext credential.
    if (!password_is_hash(stored)) {
        if (stored.substr(0, 9) == "$openads-") return false;
        return allow_legacy && constant_equal(stored, password);
    }
    const auto body = stored.substr(kPasswordPrefix.size());
    if (body.size() != 32 + 1 + 64 || body[32] != '$') return false;
    std::string salt, expected;
    if (!decode_hex(body.substr(0, 32), salt) || !decode_hex(body.substr(33), expected))
        return false;
    const auto key = pbkdf2_sha256(password, salt, 100000);
    return constant_equal(expected, std::string_view(reinterpret_cast<const char*>(key.data()), key.size()));
}

}  // namespace openads::engine