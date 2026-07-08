#include "engine/oem_collation.h"

#include <algorithm>
#include <cstring>

namespace openads::engine {
namespace {

// PL852 / NTXPL852 — Polish CP-852 collation (Harbour cppl852 / l_pl.h).
// Ł (0x9D) sorts between L (0x4C) and M (0x4D).
static constexpr OemCollation k_pl852 = {
    "PL852",
    { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,159,161,162,164,165,167,168,169,170,171,172,173,175,176,178,180,181,182,183,185,186,187,188,189,190,191,65,66,67,68,69,70,159,161,162,164,165,167,168,169,170,171,172,173,175,176,178,180,181,182,183,185,186,187,188,189,190,191,71,72,73,74,75,76,77,78,79,80,81,163,82,174,83,84,85,86,192,87,163,88,89,90,91,92,93,94,184,184,95,96,97,98,174,99,100,101,102,179,103,160,160,104,105,166,166,106,192,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,193,193,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,179,157,158,177,177,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220 }
};

bool names_equal(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) return false;
    while (*a && *b) {
        auto ca = static_cast<unsigned char>(*a++);
        auto cb = static_cast<unsigned char>(*b++);
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return *a == *b;
}

} // namespace

const OemCollation* lookup_oem_collation(const char* name) noexcept {
    if (name == nullptr) return nullptr;
    if (names_equal(name, "PL852") || names_equal(name, "NTXPL852"))
        return &k_pl852;
    return nullptr;
}

int compare_oem_keys(const std::uint8_t* sort,
                     const char* a, const char* b,
                     std::size_t cmp_len) noexcept {
    if (sort == nullptr) {
        return std::memcmp(a, b, cmp_len);
    }
    for (std::size_t i = 0; i < cmp_len; ++i) {
        const auto na = sort[static_cast<unsigned char>(a[i])];
        const auto nb = sort[static_cast<unsigned char>(b[i])];
        if (na != nb) return (na < nb) ? -1 : 1;
        if (a[i] != b[i]) {
            // Same collation weight — tie-break on raw byte (Harbour acc path).
            const auto ua = static_cast<unsigned char>(a[i]);
            const auto ub = static_cast<unsigned char>(b[i]);
            return (ua < ub) ? -1 : 1;
        }
    }
    return 0;
}

} // namespace openads::engine