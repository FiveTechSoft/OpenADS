#pragma once

// OpenADS native Data Dictionary binary format ("OADD").
//
// See docs/dd-native-format.md for the on-disk layout — that document is the
// source of truth and this header/impl are kept in lock-step with it.
//
// The dictionary is stored as a set of self-describing sections, one per
// `system.*` catalog. A section carries its column names inline and every value
// is a length-prefixed UTF-8 string, so a reader never guesses offsets and can
// skip sections/columns it does not recognise (forward compatibility).

#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::engine {

// One catalog stored in the dictionary (e.g. "indexes"). `columns` is the
// schema in order; `rows` is row-major, each inner vector the same length as
// `columns`. All values are strings (matching the ADS CICHAR catalog surface).
struct NativeSection {
    std::string                           name;
    std::vector<std::string>              columns;
    std::vector<std::vector<std::string>> rows;
};

// Current on-disk format version. Bump on any layout change (see the spec).
inline constexpr std::uint16_t kDdNativeVersion = 1;

// True if `data` begins with the "OADD" magic (cheap sniff for open()).
bool dd_is_native(const std::uint8_t* data, std::size_t len) noexcept;

// Serialise sections to the OADD binary format.
std::vector<std::uint8_t>
dd_native_encode(const std::vector<NativeSection>& sections);

// Parse the OADD binary format. Fails (never reads out of bounds) on a bad
// magic, an unsupported major version, or any length that would run past its
// enclosing bound.
util::Result<std::vector<NativeSection>>
dd_native_decode(const std::uint8_t* data, std::size_t len);

}  // namespace openads::engine
