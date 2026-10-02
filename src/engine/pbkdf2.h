#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <string>

namespace openads::engine {

// PBKDF2-HMAC-SHA256 (RFC 8018). Used for OpenADS encrypted tables (0xC4).
std::array<std::uint8_t, 32>
pbkdf2_sha256(std::string_view password,
              std::string_view salt,
              std::uint32_t iterations);

// Credential format: $openads-pbkdf2-sha256$100000$<16-byte salt hex>$<digest hex>.
// Legacy plaintext can only be checked when allow_legacy is explicitly true.
std::string hash_password(std::string_view password);
bool verify_password(std::string_view stored, std::string_view password,
                     bool allow_legacy = true);
bool password_is_hash(std::string_view stored);

// Legacy M11.2 derivation: zero-pad/truncate password to 32 bytes.
// Retained for tables encrypted with header version 0xC3.
std::array<std::uint8_t, 32>
derive_legacy_encryption_key(std::string_view password);

// Current derivation for new encryptions (header version 0xC4).
std::array<std::uint8_t, 32>
derive_encryption_key(std::string_view password);

}  // namespace openads::engine