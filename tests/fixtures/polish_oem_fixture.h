#pragma once

// Polish CP-852 test bytes: Ł is OEM 0x9D. Clang rejects adjacent
// string-literal concatenation in array initializers (-Wstring-concatenation)
// and greedy \x escapes when the next character is a hex digit (e.g. \x41B).
namespace openads::test {

inline constexpr char kPolishLab3[] = {'\x9D', 'A', 'B', '\0'};
inline constexpr char kPolishLabRow8[] =
    {'\x9D', 'A', 'B', 'B', 'B', 'B', 'B', 'B', '\0'};

} // namespace openads::test