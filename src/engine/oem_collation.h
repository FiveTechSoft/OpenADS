#pragma once

#include <cstddef>
#include <cstdint>

namespace openads::engine {

// OEM national collation sort weights (256 bytes). Index keys are compared
// byte-by-byte through this table so Polish NTXPL852 (Ł between L and M)
// matches Clipper / Harbour / SAP ACE local-server behaviour.
struct OemCollation {
    const char* name;
    std::uint8_t sort[256];
};

// nullptr when unknown; BINARY uses raw memcmp (no table).
const OemCollation* lookup_oem_collation(const char* name) noexcept;

// Compare two OEM key buffers over cmp_len bytes (prefix seek uses the
// unstripped search length). When sort is null, falls back to memcmp.
int compare_oem_keys(const std::uint8_t* sort,
                     const char* a, const char* b,
                     std::size_t cmp_len) noexcept;

} // namespace openads::engine