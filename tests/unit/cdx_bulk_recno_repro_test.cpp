// Repro: large-scale CDX bulk-load (build_bulk) must keep every key bound to
// its own recno. Report (community): an index whose key shares long prefixes
// across rows (e.g. UPPER(cNombre)) comes back with keys associated to the
// WRONG recno after a large reindex, while simple distinct-key indexes are
// fine. build_bulk packs whole leaves at once using dup/trailing-prefix
// compression, which prefix-sharing text keys exercise heavily.
#include "doctest.h"
#include "drivers/cdx/cdx_index.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::cdx::CdxIndex;

namespace {

// Walk the whole tag in key order; return the (key, recno) pairs as visited.
std::vector<std::pair<std::string, std::uint32_t>> walk(CdxIndex& ix) {
    std::vector<std::pair<std::string, std::uint32_t>> out;
    auto s = ix.seek_first();
    while (s && s.value().positioned) {
        out.emplace_back(ix.current_key(), s.value().recno);
        s = ix.next();
    }
    return out;
}

// Build a tag with `n` keys of width `klen`, then verify the ordered walk
// preserves every key->recno binding. recno is the REVERSE of key order, so a
// desync surfaces immediately. `shared_prefix` toggles between prefix-sharing
// text keys (the reported failure) and distinct keys (the control).
void check_bulk(const fs::path& dir, std::uint16_t klen, std::uint32_t n,
                bool shared_prefix) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto bag = dir / "t.cdx";

    auto created = CdxIndex::create(bag.string(), "TAG", "KEY", klen,
                                    /*unique*/ false, /*descend*/ false, "");
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();

    std::map<std::string, std::uint32_t> expected;
    std::vector<std::pair<std::string, std::uint32_t>> pairs;
    pairs.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        char buf[16];
        // shared_prefix: "PR0000".. (consecutive keys share a long prefix)
        // control:       a spread key with no long shared prefix
        std::snprintf(buf, sizeof(buf),
                      shared_prefix ? "PR%04u" : "%04u",
                      shared_prefix ? i : (i * 9301u + 49297u) % 10000u);
        std::string key(buf);
        key.resize(klen, ' ');
        const std::uint32_t recno = n - i;   // reverse of key order
        if (expected.count(key)) continue;   // keep keys unique
        expected[key] = recno;
        pairs.emplace_back(key, recno);
    }

    REQUIRE(ix.build_bulk(pairs).has_value());
    REQUIRE(ix.flush().has_value());

    auto got = walk(ix);
    REQUIRE(got.size() == expected.size());

    std::string prev;
    std::size_t mismatches = 0;
    for (const auto& [key, recno] : got) {
        if (!prev.empty()) CHECK(prev <= key);          // ascending key order
        prev = key;
        auto it = expected.find(key);
        REQUIRE(it != expected.end());
        if (it->second != recno) ++mismatches;          // key<->recno binding
    }
    CHECK(mismatches == 0);
}

} // namespace

TEST_CASE("CDX build_bulk: prefix-sharing text keys keep their recno (repro)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_bulk_recno";
    check_bulk(dir, /*klen*/ 10, /*n*/ 3000, /*shared_prefix*/ true);
    fs::remove_all(dir);
}

TEST_CASE("CDX build_bulk: distinct keys keep their recno (control)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_bulk_recno_ctl";
    check_bulk(dir, /*klen*/ 10, /*n*/ 3000, /*shared_prefix*/ false);
    fs::remove_all(dir);
}
