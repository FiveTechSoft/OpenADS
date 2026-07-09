#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

// OEM upper-casing for national collations (e.g. PL852).
// If upper_tbl is null, falls back to ASCII toupper on bytes (passthrough high bytes).
// Returns a new string of same length, upper-cased where applicable.
std::string oem_upper(const std::uint8_t* upper_tbl, const char* s, std::size_t len);

// Lookup or get upper table for a collation name (parallel to sort table).
// Returns null if no special upper table (use ASCII fallback).
const std::uint8_t* lookup_oem_upper_table(const char* name) noexcept;

} // namespace openads::engine