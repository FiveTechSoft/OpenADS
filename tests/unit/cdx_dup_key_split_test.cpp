// Regression test for Pritpal Bedi's "record 151" GPF:
//   hb_cdxPageSeekKey: wrong parent key (DBFCDX, at DBCOMMIT)
//
// Root cause: a CDX branch entry must always hold its child's MAXIMUM
// (key, recno) pair. OpenADS refreshed the separator only for the
// rightmost entry and compared key TEXT alone, so inserting a duplicate
// key with a higher recno left the parent separator stale. Harbour
// validates child-max == parent-key on descent and aborts.
//
// This test builds a duplicate-heavy tag (10 distinct names x 16 rounds,
// B_BIG's exact pattern) and then walks the raw .cdx pages with an
// INDEPENDENT decoder, asserting:
//   1. every branch entry pair equals its child subtree's max pair;
//   2. the leaf chain is globally nondecreasing in (key, recno);
//   3. every inserted pair is present exactly once.

#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

namespace {

using Pair = std::pair<std::string, std::uint32_t>;  // (key, recno)

std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])       |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint32_t rd32be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

struct PageFile {
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 512> page(std::uint32_t off) const {
        std::array<std::uint8_t, 512> out{};
        if (off + 512 <= bytes.size()) {
            std::memcpy(out.data(), bytes.data() + off, 512);
        }
        return out;
    }
};

PageFile load(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return PageFile{std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>())};
}

// Independent compact-leaf decoder (mirrors Harbour hb_cdxPageLeafDecode,
// NOT OpenADS' own decode path — the point is an external reader's view).
std::vector<Pair> decode_leaf(const std::array<std::uint8_t, 512>& pg,
                              std::uint16_t klen) {
    const std::uint16_t n     = rd16(pg.data() + 2);
    const std::uint32_t rmask = rd32le(pg.data() + 14);
    const std::uint8_t  dmask = pg[18];
    const std::uint8_t  tmask = pg[19];
    const std::uint8_t  dbits = pg[21];
    const std::uint8_t  tbits = pg[22];
    const std::uint8_t  reqb  = pg[23];
    const int           ibits = 32 - tbits - dbits;

    std::vector<Pair> out;
    std::size_t kpos = 512;   // key suffixes grow left from the page end
    std::string prev(klen, ' ');
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::uint8_t* e = pg.data() + 24 + i * reqb;
        const std::uint32_t rec = rd32le(e) & rmask;
        const std::uint32_t tmp = rd32le(e + reqb - 4) >> ibits;
        const std::uint32_t dup = (i == 0) ? 0u : (tmp & dmask);
        const std::uint32_t trl = (tmp >> dbits) & tmask;
        const std::uint32_t inew = klen - dup - trl;
        kpos -= inew;
        std::string key = prev.substr(0, dup);
        key.append(reinterpret_cast<const char*>(pg.data()) + kpos, inew);
        key.append(trl, ' ');
        out.emplace_back(key, rec);
        prev = key;
    }
    return out;
}

struct BranchEnt { Pair pair; std::uint32_t child; };

std::vector<BranchEnt> decode_branch(const std::array<std::uint8_t, 512>& pg,
                                     std::uint16_t klen) {
    const std::uint16_t n = rd16(pg.data() + 2);
    const std::size_t stride = static_cast<std::size_t>(klen) + 8;
    std::vector<BranchEnt> out;
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::uint8_t* e = pg.data() + 12 + i * stride;
        BranchEnt be;
        be.pair.first.assign(reinterpret_cast<const char*>(e), klen);
        be.pair.second = rd32be(e + klen);
        be.child       = rd32be(e + klen + 4);
        out.push_back(std::move(be));
    }
    return out;
}

// Harbour's descent invariant: branch entry pair == child subtree max.
Pair check_subtree(const PageFile& f, std::uint32_t off, std::uint16_t klen,
                   std::vector<Pair>* all_keys) {
    auto pg = f.page(off);
    const std::uint16_t attr = rd16(pg.data());
    if (attr & 0x0002) {  // CDX_NODE_LEAF
        auto pairs = decode_leaf(pg, klen);
        REQUIRE(!pairs.empty());
        for (std::size_t i = 1; i < pairs.size(); ++i) {
            CHECK(pairs[i - 1] <= pairs[i]);   // (key, recno) nondecreasing
        }
        if (all_keys != nullptr) {
            all_keys->insert(all_keys->end(), pairs.begin(), pairs.end());
        }
        return pairs.back();
    }
    auto entries = decode_branch(pg, klen);
    REQUIRE(!entries.empty());
    Pair submax{};
    for (const auto& ent : entries) {
        submax = check_subtree(f, ent.child, klen, all_keys);
        INFO("branch entry pair must equal child max pair");
        CHECK(ent.pair == submax);
    }
    return submax;
}

constexpr const char* kNames[10] = {"Alice", "Bob", "Phillip", "Charlie",
                                    "Linda", "Finland", "Diana", "Lucy",
                                    "Jony", "Edward"};

} // namespace

TEST_CASE("CDX duplicate-key inserts keep parent separators exact (record-151 regression)") {
    auto p = fs::temp_directory_path() / "openads_dupkey_split.cdx";
    fs::remove(p);

    constexpr std::uint16_t klen = 19;   // B_BIG NAME C,19
    constexpr int kRounds = 16;          // 16 x 10 records -> forces splits
    {
        auto created = CdxIndex::create(p.string(), "IDX01", "NAME", klen,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int round = 0; round < kRounds; ++round) {
            for (int i = 0; i < 10; ++i) {
                const std::uint32_t recno =
                    static_cast<std::uint32_t>(round * 10 + i + 1);
                REQUIRE(ix.insert(recno, kNames[i]).has_value());
            }
        }
        REQUIRE(ix.flush().has_value());
    }

    auto f = load(p);
    REQUIRE(f.bytes.size() >= 2560 + 1024);
    // Tag header sits at CDX_SUB_HEADER_OFFSET (1536); root pointer at +0.
    const std::uint32_t root = rd32le(f.bytes.data() + 1536);
    REQUIRE(root != 0);

    std::vector<Pair> all;
    check_subtree(f, root, klen, &all);

    // Every inserted pair exactly once, globally ordered.
    CHECK(all.size() == static_cast<std::size_t>(kRounds * 10));
    std::vector<Pair> expect;
    for (int round = 0; round < kRounds; ++round) {
        for (int i = 0; i < 10; ++i) {
            std::string key = kNames[i];
            key.append(klen - key.size(), ' ');
            expect.emplace_back(key,
                static_cast<std::uint32_t>(round * 10 + i + 1));
        }
    }
    std::sort(expect.begin(), expect.end());
    CHECK(all == expect);

    fs::remove(p);
}

namespace {

// Shared fixture: `rounds` x 10 duplicate names (B_BIG pattern) into a
// fresh C,19 tag. Returns the file path; caller deletes.
fs::path build_dup_tree(const char* fname, int rounds) {
    auto p = fs::temp_directory_path() / fname;
    fs::remove(p);
    constexpr std::uint16_t klen = 19;
    auto created = CdxIndex::create(p.string(), "IDX01", "NAME", klen,
                                    false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();
    for (int round = 0; round < rounds; ++round) {
        for (int i = 0; i < 10; ++i) {
            const std::uint32_t recno =
                static_cast<std::uint32_t>(round * 10 + i + 1);
            REQUIRE(ix.insert(recno, kNames[i]).has_value());
        }
    }
    REQUIRE(ix.flush().has_value());
    return p;
}

void structural_check(const fs::path& p, std::vector<Pair>& all) {
    auto f = load(p);
    REQUIRE(f.bytes.size() >= 2560);   // through the sub-tag header
    const std::uint32_t root = rd32le(f.bytes.data() + 1536);
    REQUIRE(root != 0);
    check_subtree(f, root, 19, &all);
}

} // namespace

TEST_CASE("CDX erase of a leaf maximum keeps parent separators exact") {
    auto p = build_dup_tree("openads_dupkey_erase.cdx", 16);

    // Delete the three highest "Edward" pairs (each is its leaf's max at
    // the time of deletion), then keep appending past them.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        REQUIRE(ix.erase(160, "Edward").has_value());
        REQUIRE(ix.erase(150, "Edward").has_value());
        REQUIRE(ix.erase(140, "Edward").has_value());
        for (std::uint32_t r = 161; r <= 190; ++r) {
            REQUIRE(ix.insert(r, kNames[(r - 1) % 10]).has_value());
        }
        REQUIRE(ix.flush().has_value());
    }

    std::vector<Pair> all;
    structural_check(p, all);
    CHECK(all.size() == static_cast<std::size_t>(160 - 3 + 30));
    // The deleted 140/150/160 Edwards are gone; the new ones are in.
    for (const auto& kv : all) {
        if (kv.first == std::string("Edward") + std::string(13, ' ')) {
            CHECK(kv.second != 140);
            CHECK(kv.second != 150);
            CHECK(kv.second != 160);
        }
    }
    fs::remove(p);
}

TEST_CASE("CDX duplicate run spanning leaf splits seeks first and walks whole run") {
    auto p = fs::temp_directory_path() / "openads_duprun_split.cdx";
    fs::remove(p);
    constexpr std::uint32_t kN = 300;   // one key, 300 recnos -> splits
    {
        auto created = CdxIndex::create(p.string(), "IDX01", "NAME", 19,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (std::uint32_t r = 1; r <= kN; ++r) {
            REQUIRE(ix.insert(r, "SAMEKEY").has_value());
        }
        REQUIRE(ix.flush().has_value());

        // Seek lands on the first recno of the run.
        auto sk = ix.seek_key("SAMEKEY", false);
        REQUIRE(sk.has_value());
        CHECK(sk.value().hit == SeekHit::Exact);
        CHECK(sk.value().recno == 1);
        // Walk crosses every split boundary, recnos ascending.
        std::uint32_t prev = 0;
        int n = 0;
        auto cur = sk;
        while (cur.has_value() && cur.value().positioned) {
            CHECK(cur.value().recno > prev);
            prev = cur.value().recno;
            ++n;
            cur = ix.next();
        }
        CHECK(n == static_cast<int>(kN));
    }

    std::vector<Pair> all;
    structural_check(p, all);
    CHECK(all.size() == static_cast<std::size_t>(kN));
    fs::remove(p);
}

TEST_CASE("CDX recno-bit growth keeps duplicate order past 16383") {
    auto p = fs::temp_directory_path() / "openads_rnbits_grow.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "IDX01", "NAME", 19,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int i = 0; i < 10; ++i) {
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i + 1),
                              kNames[i]).has_value());
        }
        for (std::uint32_t r = 16380; r <= 16420; ++r) {
            REQUIRE(ix.insert(r, kNames[(r - 1) % 10]).has_value());
        }
        REQUIRE(ix.flush().has_value());

        auto sk = ix.seek_key("Edward", false);
        REQUIRE(sk.has_value());
        CHECK(sk.value().recno == 10);   // first Edward wins
    }

    std::vector<Pair> all;
    structural_check(p, all);
    CHECK(all.size() == static_cast<std::size_t>(10 + 41));
    fs::remove(p);
}
