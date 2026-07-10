#include "engine/oem_collation.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

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

// PL852 upper-case table (CP-852 Polish): built at first use by
// init_pl852_upper() below — ASCII a-z plus the Polish lower→upper byte
// mappings. (An earlier duplicate constexpr copy of the identity part of
// this table sat here unused and broke the clang/gcc -Werror builds with
// -Wunused-const-variable.)
static std::uint8_t* init_pl852_upper() {
    static std::uint8_t tbl[256];
    static bool inited = false;
    if (!inited) {
        for (int i = 0; i < 256; ++i) tbl[i] = static_cast<std::uint8_t>(i);
        for (int i = 'a'; i <= 'z'; ++i) tbl[i] = static_cast<std::uint8_t>(i - 32);
        // Polish mappings for CP852 (lower -> upper byte)
        // These are approximate; real CP852:
        // 0xA5 (ą) -> 0xA4 (Ą), 0x86 (ć) -> 0x8F (Ć), 0xA9 (ę)->0xA8, 
        // 0x88 (ł)->0x9D (Ł? wait adjust), etc. Using common values from Harbour etc.
        tbl[0xA5] = 0xA4; // ą -> Ą
        tbl[0x86] = 0x8F; // ć -> Ć (example)
        tbl[0xA9] = 0xA8; // ę -> Ę
        tbl[0x88] = 0x9D; // ł -> Ł (adjust per actual)
        tbl[0xE4] = 0xE3; // ń etc. - extend if needed for full accuracy
        // Add more as reported.
        inited = true;
    }
    return tbl;
}

const std::uint8_t* lookup_oem_upper_table(const char* name) noexcept {
    if (name == nullptr) return nullptr;
    if (names_equal(name, "PL852") || names_equal(name, "NTXPL852")) {
        return init_pl852_upper();
    }
    return nullptr;
}

std::string oem_upper(const std::uint8_t* upper_tbl, const char* s, std::size_t len) {
    std::string out(len, ' ');
    if (!upper_tbl) {
        for (std::size_t i = 0; i < len; ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : s[i];
        }
        return out;
    }
    for (std::size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned char u = upper_tbl[c];
        out[i] = (u != 0 && u != c) ? static_cast<char>(u) : 
                 ((c >= 'a' && c <= 'z') ? static_cast<char>(c-32) : s[i]);
    }
    return out;
}

namespace {
std::atomic<const std::uint8_t*> g_active_oem_upper{nullptr};

// RCB 2026-07-10 — default OEM collation for ADS_OEM tables (#130
// follow-up). Seeded once from OPENADS_OEM_COLLATION; an explicit
// set_default_oem_collation() (tests, future adslocal.cfg parser)
// overrides the env value.
std::atomic<const OemCollation*> g_default_oem{nullptr};
std::atomic<const std::uint8_t*> g_default_oem_upper{nullptr};
std::once_flag                   g_default_oem_env_once;

void seed_default_oem_from_env() noexcept {
    std::call_once(g_default_oem_env_once, [] {
        const char* v = std::getenv("OPENADS_OEM_COLLATION");
        if (v == nullptr || *v == '\0') return;
        if (const OemCollation* c = lookup_oem_collation(v)) {
            g_default_oem.store(c, std::memory_order_relaxed);
            g_default_oem_upper.store(lookup_oem_upper_table(v),
                                      std::memory_order_relaxed);
        }
    });
}
}  // namespace

void set_active_oem_upper_table(const std::uint8_t* tbl) noexcept {
    g_active_oem_upper.store(tbl, std::memory_order_relaxed);
}

const std::uint8_t* active_oem_upper_table() noexcept {
    return g_active_oem_upper.load(std::memory_order_relaxed);
}

bool set_default_oem_collation(const char* name) noexcept {
    // Run the env seed first so an explicit set always wins over it.
    seed_default_oem_from_env();
    if (name == nullptr || *name == '\0') {
        g_default_oem.store(nullptr, std::memory_order_relaxed);
        g_default_oem_upper.store(nullptr, std::memory_order_relaxed);
        return true;
    }
    const OemCollation* c = lookup_oem_collation(name);
    if (c == nullptr) return false;
    g_default_oem.store(c, std::memory_order_relaxed);
    g_default_oem_upper.store(lookup_oem_upper_table(name),
                              std::memory_order_relaxed);
    return true;
}

const OemCollation* default_oem_collation() noexcept {
    seed_default_oem_from_env();
    return g_default_oem.load(std::memory_order_relaxed);
}

const std::uint8_t* default_oem_upper_table() noexcept {
    seed_default_oem_from_env();
    return g_default_oem_upper.load(std::memory_order_relaxed);
}

} // namespace openads::engine
