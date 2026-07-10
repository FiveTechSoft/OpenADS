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

// Process-wide "currently active" OEM upper table for expression
// evaluation. UPPER() / the upper_bare fast path in index_expr run
// without a Connection at hand, so AdsSetCollation publishes the
// selected collation's upper table here (and clears it on
// BINARY/NOCASE). nullptr — the default — means no OEM collation is
// active and UPPER falls back to UTF-8 case promotion.
void set_active_oem_upper_table(const std::uint8_t* tbl) noexcept;
const std::uint8_t* active_oem_upper_table() noexcept;

// RCB 2026-07-10 — process-wide DEFAULT OEM collation (issue #130
// follow-up, zero-config OEM activation). SAP semantics: the OEM
// collation language is a machine-level setting (ADSLOCAL.CFG, written
// at client install time); a client only says "this table is OEM" via
// usCharType=ADS_OEM at AdsOpenTable. Harbour rddads NEVER calls
// AdsSetCollation (it has no such call — AdsSetCharType() only feeds
// usCharType), so without this default a rddads app has NO way to
// activate PL852 and every INDEX ON builds keys with UTF-8 casing and
// binary sort. Tables opened with ADS_OEM fall back to this default
// when their connection has no explicit AdsSetCollation override; see
// Table::oem_upper_table() / Table::oem_sort_table(). Populated from
// the OPENADS_OEM_COLLATION environment variable on first use, or
// explicitly via set_default_oem_collation() (adslocal.cfg parsing —
// Phase 2 — will call the same setter). Do NOT remove this in favour
// of connection-only activation: that re-breaks OEM apps (temp notes
// 2026-07-10, contributor regression report on v1.8.6/v1.8.7).
// set_default_oem_collation(nullptr/"") clears; returns false for an
// unknown collation name.
bool set_default_oem_collation(const char* name) noexcept;
const OemCollation* default_oem_collation() noexcept;
const std::uint8_t* default_oem_upper_table() noexcept;

} // namespace openads::engine