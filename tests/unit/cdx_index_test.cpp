#include "doctest.h"
#include "drivers/cdx/cdx_index.h"
#include "drivers/index_trait.h"
#include "engine/index_expr.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::KeyEncoding;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

TEST_CASE("CdxIndex create + insert + reopen walks keys in compact-leaf order") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_basic.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(2, "AAAA").has_value());
        REQUIRE(ix.insert(3, "BBBB").has_value());
        REQUIRE(ix.insert(1, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        CHECK(ix.expression() == "TAG");
        CHECK(ix.name()       == "T1");

        auto a = ix.seek_first();
        REQUIRE(a.has_value());
        CHECK(a.value().recno == 2);
        CHECK(ix.current_key() == "AAAA");

        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "BBBB");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "CCCC");
        auto end = ix.next();
        REQUIRE(end.has_value());
        CHECK_FALSE(end.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex seek_key locates an exact match") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_seek.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto r = ix.seek_key("BBBB", false);
        REQUIRE(r.has_value());
        CHECK(r.value().hit == SeekHit::Exact);
        CHECK(r.value().recno == 2);
        CHECK(ix.current_key() == "BBBB");
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex seek_last + prev walks backward") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_back.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto r = ix.seek_last();
        REQUIRE(r.has_value());
        CHECK(ix.current_key() == "CCCC");
        REQUIRE(ix.prev().has_value());
        CHECK(ix.current_key() == "BBBB");
        REQUIRE(ix.prev().has_value());
        CHECK(ix.current_key() == "AAAA");
        auto begin = ix.prev();
        REQUIRE(begin.has_value());
        CHECK_FALSE(begin.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex erase removes a key") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_erase.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.insert(3, "CCCC").has_value());
        REQUIRE(ix.erase(2, "BBBB").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "AAAA");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "CCCC");
        auto end = ix.next();
        REQUIRE(end.has_value());
        CHECK_FALSE(end.value().positioned);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex compound layout: file header + struct-tag leaf + sub-tag header") {
    auto p = fs::temp_directory_path() / "openads_m39_cdx_compound.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "MYTAG", "FIELD", 8, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "alpha").has_value());
        REQUIRE(ix.insert(2, "beta").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // Direct on-disk inspection: file header at 0, struct-tag root leaf at
    // offset 1024 mapping the tag name to 1536, sub-tag header at 1536.
    {
        std::ifstream f(p, std::ios::binary);
        REQUIRE(f.is_open());
        std::uint8_t hdr[1024]{};
        f.read(reinterpret_cast<char*>(hdr), 1024);
        auto rd32 = [](const std::uint8_t* x) {
            return  static_cast<std::uint32_t>(x[0])        |
                   (static_cast<std::uint32_t>(x[1]) <<  8) |
                   (static_cast<std::uint32_t>(x[2]) << 16) |
                   (static_cast<std::uint32_t>(x[3]) << 24);
        };
        auto rd16 = [](const std::uint8_t* x) {
            return  static_cast<std::uint16_t>(x[0]) |
                   (static_cast<std::uint16_t>(x[1]) << 8);
        };
        CHECK(rd32(hdr + 0) == 1024u);                 // struct-tag root
        CHECK(rd16(hdr + 12) == 10u);                  // struct-tag key_size = 10
        CHECK(static_cast<int>(hdr[14] & 0x40) != 0);  // CDX_TYPE_COMPOUND bit
        // Native-interop conformance: the structure ("tag of tags") header
        // MUST carry CDX_TYPE_STRUCTURE (0x80); without it a native reader
        // tries to compile the tag-name "key expression" and reports the
        // index corrupt.
        CHECK(static_cast<int>(hdr[14] & 0x80) != 0);  // CDX_TYPE_STRUCTURE

        f.seekg(1024);
        std::uint8_t leaf[512]{};
        f.read(reinterpret_cast<char*>(leaf), 512);
        // Root-that-is-a-leaf must be ROOT|LEAF (0x03); a native FoxPro /
        // Harbour reader rejects a root node missing the ROOT bit.
        CHECK(rd16(leaf + 0) == 3u);                   // CDX_NODE_ROOT | CDX_NODE_LEAF
        CHECK(rd16(leaf + 2) == 1u);                   // one entry

        f.seekg(1536);
        std::uint8_t sub[1024]{};
        f.read(reinterpret_cast<char*>(sub), 1024);
        CHECK(rd16(sub + 12) == 8u);                   // sub-tag key_size
    }

    // Reopen via the public API and confirm the sub-tag walks correctly.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        CHECK(ix.name() == "MYTAG");
        CHECK(ix.expression() == "FIELD");
        CHECK(ix.key_length() == 8);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "alpha   ");     // padded to 8
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "beta    ");
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex multi-tag: sibling tags don't collide on page allocation") {
    // Reproduces the multi-tag interop bug: two tags created in the same
    // .cdx before any record is inserted (CREATE INDEX twice on an empty
    // table), then both populated. Each CdxIndex instance tracked its own
    // file_size_, so tag1's first data page used to land on top of tag2's
    // header. alloc_page_ now reserves each page at the real end-of-file.
    auto p = fs::temp_directory_path() / "openads_cdx_multitag_alloc.cdx";
    fs::remove(p);
    {
        auto t1 = CdxIndex::create(p.string(), "TAG1", "F1", 10, false, false);
        REQUIRE(t1.has_value());
        CdxIndex ix1 = std::move(t1).value();
        auto t2 = CdxIndex::add_tag(p.string(), "TAG2", "F2", 10, false, false);
        REQUIRE(t2.has_value());
        CdxIndex ix2 = std::move(t2).value();
        for (std::uint32_t r = 1; r <= 30; ++r) {
            char k1[8], k2[8];
            std::snprintf(k1, sizeof(k1), "a%02u", r);
            std::snprintf(k2, sizeof(k2), "b%02u", r);
            REQUIRE(ix1.insert(r, k1).has_value());
            REQUIRE(ix2.insert(r, k2).has_value());
        }
        REQUIRE(ix1.flush().has_value());
        REQUIRE(ix2.flush().has_value());
    }

    // The structure-tag root leaf must still carry ROOT|LEAF after add_tag
    // re-encoded it.
    {
        std::ifstream f(p, std::ios::binary);
        REQUIRE(f.is_open());
        f.seekg(1024);
        std::uint8_t leaf[512]{};
        f.read(reinterpret_cast<char*>(leaf), 512);
        CHECK((leaf[0] | (leaf[1] << 8)) == 3);   // ROOT | LEAF
    }

    // Both tags must reopen and walk all 30 keys — proof neither sub-tag's
    // pages were overwritten by the other's.
    auto walk_count = [&](const char* tag) {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, tag)
                    .has_value());
        int n = 0;
        auto o = ix.seek_first();
        REQUIRE(o.has_value());
        while (o.value().positioned) {
            ++n;
            o = ix.next();
            REQUIRE(o.has_value());
        }
        return n;
    };
    CHECK(walk_count("TAG1") == 30);
    CHECK(walk_count("TAG2") == 30);
    fs::remove(p);
}

TEST_CASE("CdxIndex multi-tag: add_tag + open_named round-trip independent sub-trees") {
    auto p = fs::temp_directory_path() / "openads_m310_cdx_multitag.cdx";
    fs::remove(p);

    // 1) Create the CDX with a first tag and insert into it.
    {
        auto created = CdxIndex::create(p.string(), "PRIMARY", "PK", 4, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        REQUIRE(ix.insert(2, "BBBB").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // 2) Add a second tag with a different key length and key expression,
    //    then insert independent rows into it.
    {
        auto added = CdxIndex::add_tag(p.string(), "SECOND", "EXPR", 6, true, false);
        REQUIRE(added.has_value());
        CdxIndex ix = std::move(added).value();
        REQUIRE(ix.insert(10, "alpha1").has_value());
        REQUIRE(ix.insert(20, "beta22").has_value());
        REQUIRE(ix.flush().has_value());
    }

    // 3) list_tags reports both.
    {
        auto tags = CdxIndex::list_tags(p.string());
        REQUIRE(tags.has_value());
        auto& tv = tags.value();
        REQUIRE(tv.size() == 2);
        // Sorted by tag name.
        CHECK(tv[0] == "PRIMARY");
        CHECK(tv[1] == "SECOND");
    }

    // 4) open_named picks each sub-tag independently and walks its keys.
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "PRIMARY")
                .has_value());
        CHECK(ix.name() == "PRIMARY");
        CHECK(ix.expression() == "PK");
        CHECK(ix.key_length() == 4);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "AAAA");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "BBBB");
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open_named(p.string(), IndexOpenMode::Shared, "SECOND")
                .has_value());
        CHECK(ix.name() == "SECOND");
        CHECK(ix.expression() == "EXPR");
        CHECK(ix.key_length() == 6);
        CHECK(ix.unique() == true);
        REQUIRE(ix.seek_first().has_value());
        CHECK(ix.current_key() == "alpha1");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "beta22");
    }

    // 5) open_named with a non-existent tag returns 5044.
    {
        CdxIndex ix;
        auto r = ix.open_named(p.string(), IndexOpenMode::Shared, "NOPE");
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }

    // 6) add_tag rejects duplicate tag names.
    {
        auto r = CdxIndex::add_tag(p.string(), "PRIMARY", "X", 4, false, false);
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }

    fs::remove(p);
}

TEST_CASE("CdxIndex unique tag rejects duplicates") {
    auto p = fs::temp_directory_path() / "openads_m35_cdx_unique.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4, true, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        REQUIRE(ix.insert(1, "AAAA").has_value());
        auto r = ix.insert(2, "AAAA");
        CHECK_FALSE(r.has_value());
        CHECK(r.error().code == 5044);
    }
    fs::remove(p);
}

// Harbour dbfcdx uses bTrail='\0' for non-character keys so trailing
// zero bytes of FoxNumeric IEEE doubles are elided. OpenADS used to
// always trail-pack on spaces only, leaving numeric leaves ~2× bulkier
// than native DBFCDX for the same key set.
TEST_CASE("CdxIndex FoxNumeric bulk packs trailing NULs like Harbour DBFCDX") {
    auto p = fs::temp_directory_path() / "openads_cdx_foxnum_trail.cdx";
    fs::remove(p);

    std::vector<std::pair<std::string, std::uint32_t>> bulk;
    bulk.reserve(10);
    for (std::uint32_t r = 1; r <= 10; ++r) {
        bulk.emplace_back(openads::engine::fox_numeric_key(
                              static_cast<double>(r)),
                          r);
    }

    {
        auto created = CdxIndex::create(p.string(), "INS", "INS", 8,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        ix.set_key_encoding(KeyEncoding::FoxNumeric);
        REQUIRE(ix.build_bulk(bulk).has_value());
        REQUIRE(ix.flush().has_value());
    }

    // Walk order and seek must survive null-trail pack/unpack.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        ix.set_key_encoding(KeyEncoding::FoxNumeric);

        auto first = ix.seek_first();
        REQUIRE(first.has_value());
        CHECK(first.value().positioned);
        CHECK(first.value().recno == 1);
        CHECK(ix.current_key() == openads::engine::fox_numeric_key(1.0));

        for (std::uint32_t r = 2; r <= 10; ++r) {
            auto n = ix.next();
            REQUIRE(n.has_value());
            CHECK(n.value().positioned);
            CHECK(n.value().recno == r);
            CHECK(ix.current_key() ==
                  openads::engine::fox_numeric_key(static_cast<double>(r)));
        }
        auto end = ix.next();
        REQUIRE(end.has_value());
        CHECK_FALSE(end.value().positioned);

        auto hit = ix.seek_key(openads::engine::fox_numeric_key(7.0), false);
        REQUIRE(hit.has_value());
        CHECK(hit.value().hit == SeekHit::Exact);
        CHECK(hit.value().recno == 7);
    }

    // On-disk leaf free space for 10 FoxNumeric keys should match the
    // Harbour packing density (free >= 440; space-only trail left free~386).
    {
        std::ifstream f(p, std::ios::binary);
        REQUIRE(f.is_open());
        // Sub-tag data leaf sits at the first data page (offset 2560
        // for a single-tag bag with one leaf).
        f.seekg(2560);
        std::uint8_t leaf[512]{};
        f.read(reinterpret_cast<char*>(leaf), 512);
        auto rd16 = [](const std::uint8_t* x) -> std::uint16_t {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(x[0]) |
                (static_cast<std::uint16_t>(x[1]) << 8));
        };
        CHECK(rd16(leaf + 0) == 3u);   // ROOT|LEAF
        CHECK(rd16(leaf + 2) == 10u);  // 10 keys
        const std::uint16_t free_spc = rd16(leaf + 12);
        // Harbour packs the same 10 doubles into free=447; require we
        // are in that ballpark (not the old free=386 space-only path).
        CHECK(free_spc >= 440);
        CHECK(free_spc <= 460);
    }
    fs::remove(p);
}

TEST_CASE("CdxIndex Text keys still trail-pack trailing spaces") {
    auto p = fs::temp_directory_path() / "openads_cdx_text_trail.cdx";
    fs::remove(p);
    {
        auto created = CdxIndex::create(p.string(), "NM", "NAME", 20,
                                        false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        // Default Text encoding — trail byte is space.
        REQUIRE(ix.insert(1, "Alice").has_value());
        REQUIRE(ix.insert(2, "Bob").has_value());
        REQUIRE(ix.flush().has_value());
    }
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        REQUIRE(ix.seek_first().has_value());
        // Decoded key is space-padded to key length.
        CHECK(ix.current_key() == "Alice               ");
        REQUIRE(ix.next().has_value());
        CHECK(ix.current_key() == "Bob                 ");
    }
    fs::remove(p);
}

// Pritpal Bedi 2026-08-06: after a large bag lived at path P, CREATE INDEX
// (CreateRW truncate) of a small bag at the same path reused the process-
// wide page allocator tail and reserved pages far past EOF → .cdx size
// ballooned ~10x with sparse holes and SKIP walked in erratic order.
TEST_CASE("CdxIndex create resets page allocator tail after recreate at same path") {
    auto p = fs::temp_directory_path() / "openads_cdx_alloc_tail_recreate.cdx";
    fs::remove(p);

    std::uintmax_t big_sz = 0;
    {
        auto created = CdxIndex::create(p.string(), "K", "K", 12, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        // Enough keys that a sticky process-wide allocator tail would leave
        // a multi-10 KB gap after recreate (dense pack still > 12 KB).
        for (std::uint32_t r = 1; r <= 3000; ++r) {
            char k[16];
            std::snprintf(k, sizeof(k), "K%06u", r);
            REQUIRE(ix.insert(r, k).has_value());
        }
        REQUIRE(ix.flush().has_value());
        big_sz = fs::file_size(p);
        REQUIRE(big_sz > 12 * 1024);
    }

    // Same process, same path: CreateRW truncates. Insert a handful of keys.
    std::uintmax_t small_sz = 0;
    {
        auto created = CdxIndex::create(p.string(), "K", "K", 12, false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (std::uint32_t r = 1; r <= 10; ++r) {
            char k[16];
            std::snprintf(k, sizeof(k), "S%06u", r);
            REQUIRE(ix.insert(r, k).has_value());
        }
        REQUIRE(ix.flush().has_value());
        small_sz = fs::file_size(p);

        // Key order must be sorted, not erratic.
        REQUIRE(ix.seek_first().has_value());
        std::string prev;
        int n = 0;
        for (;;) {
            auto k = ix.current_key();
            if (n > 0) CHECK(k >= prev);
            prev = k;
            ++n;
            auto nx = ix.next();
            REQUIRE(nx.has_value());
            if (!nx.value().positioned) break;
        }
        CHECK(n == 10);
    }

    // Healthy small bag is a few KB. A sticky allocator tail would leave the
    // bag near `big_sz` (pages reserved from the old EOF onward).
    MESSAGE("big=" << big_sz << " small=" << small_sz);
    CHECK(small_sz < 8 * 1024);
    CHECK(small_sz * 3 < big_sz);
    fs::remove(p);
}
