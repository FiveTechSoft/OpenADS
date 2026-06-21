#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

// M(cdx-split) — exercise the multi-level B+tree path that lands in
// 0.2.x. The 512-byte CDX page with 4-byte keys holds ~110 entries
// per leaf; 5000 sequential inserts force at least one branch level
// above hundreds of leaves, so the test catches both leaf-split and
// branch-split bugs (and the new-root case when the root leaf splits).
TEST_CASE("CDX multi-level split survives many sequential inserts") {
    auto p = fs::temp_directory_path() / "openads_cdx_multilevel.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 5000;
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4,
                                         /*unique*/ false, /*descend*/ false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int i = 1; i <= N; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto e = ix.insert(static_cast<std::uint32_t>(i),
                               std::string(buf, 4));
            INFO("insert i=" << i);
            REQUIRE(e.has_value());
        }
        REQUIRE(ix.flush().has_value());
    }

    // Reopen + every key must be reachable via seek_key.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        for (int i = 1; i <= N; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto seek = ix.seek_key(std::string(buf, 4), false);
            INFO("seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }

    // Walk in order: must visit every recno in sorted-key order with
    // a monotonically non-decreasing key sequence.
    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        auto seek = ix.seek_first();
        REQUIRE(seek.has_value());
        int seen = 0;
        std::string prev_key;
        while (seek.value().positioned) {
            ++seen;
            std::string cur = ix.current_key();
            if (!prev_key.empty()) {
                CHECK(cur >= prev_key);
            }
            prev_key = cur;
            seek = ix.next();
            REQUIRE(seek.has_value());
        }
        CHECK(seen == N);
    }

    fs::remove(p, ec);
}

TEST_CASE("CDX multi-level erase removes a key from a branch tree") {
    auto p = fs::temp_directory_path() / "openads_cdx_multilevel_erase.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 5000;
    constexpr int TARGET = 2500;
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4,
                                         false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int i = 1; i <= N; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            REQUIRE(ix.insert(static_cast<std::uint32_t>(i),
                              std::string(buf, 4)).has_value());
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d", TARGET);
        REQUIRE(ix.erase(static_cast<std::uint32_t>(TARGET),
                         std::string(buf, 4)).has_value());
        REQUIRE(ix.flush().has_value());
    }

    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d", TARGET);
        auto miss = ix.seek_key(std::string(buf, 4), false);
        REQUIRE(miss.has_value());
        CHECK_FALSE(miss.value().positioned);

        for (int i = 1; i <= N; ++i) {
            if (i == TARGET) continue;
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto seek = ix.seek_key(std::string(buf, 4), false);
            INFO("seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }

    fs::remove(p, ec);
}

// Reverse-order inserts hit the new-key-is-rightmost branch path
// AND force every branch entry's key to shift, so this catches
// stale-separator bugs that the ascending case can mask.
TEST_CASE("CDX multi-level split with descending insert order") {
    auto p = fs::temp_directory_path() / "openads_cdx_multilevel_desc.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    constexpr int N = 2000;
    {
        auto created = CdxIndex::create(p.string(), "T1", "TAG", 4,
                                         false, false);
        REQUIRE(created.has_value());
        CdxIndex ix = std::move(created).value();
        for (int i = N; i >= 1; --i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto e = ix.insert(static_cast<std::uint32_t>(i),
                               std::string(buf, 4));
            INFO("insert i=" << i);
            REQUIRE(e.has_value());
        }
        REQUIRE(ix.flush().has_value());
    }

    {
        CdxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared).has_value());
        for (int i = 1; i <= N; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d", i);
            auto seek = ix.seek_key(std::string(buf, 4), false);
            INFO("seek i=" << i);
            REQUIRE(seek.has_value());
            CHECK(seek.value().hit == SeekHit::Exact);
            CHECK(seek.value().recno == static_cast<std::uint32_t>(i));
        }
    }

    fs::remove(p, ec);
}
