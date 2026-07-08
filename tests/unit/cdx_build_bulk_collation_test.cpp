#include "doctest.h"
#include "drivers/cdx/cdx_index.h"
#include "engine/oem_collation.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::SeekHit;
using openads::drivers::cdx::CdxIndex;

namespace {

std::string pad8(const char* s) {
    std::string k(s);
    if (k.size() < 8) k.append(8 - k.size(), ' ');
    return k;
}

} // namespace

TEST_CASE("CDX build_bulk sorts keys with NTXPL852 collation") {
    auto p = fs::temp_directory_path() / "openads_cdx_bulk_pl852.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    const auto* coll = openads::engine::lookup_oem_collation("NTXPL852");
    REQUIRE(coll != nullptr);

    auto created = CdxIndex::create(p.string(), "BYNAME", "NAME", 8,
                                    false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();
    ix.set_oem_sort_table(coll->sort);

    // Insert keys in binary order (Z before Ł) — bulk sort must reorder.
    std::vector<std::pair<std::string, std::uint32_t>> keys = {
        {pad8("ZBYDDDDD"), 4},
        {pad8("LACAAAAA"), 1},
        {pad8("\x9D""ABBBBBB"), 2},
        {pad8("MADCCCCC"), 3},
    };
    REQUIRE(ix.build_bulk(std::move(keys)).has_value());
    REQUIRE(ix.flush().has_value());

    auto s = ix.seek_first();
    REQUIRE(s.has_value());
    REQUIRE(s.value().positioned);
    CHECK(s.value().recno == 1u);
    CHECK(ix.current_key().substr(0, 3) == "LAC");

    s = ix.next();
    REQUIRE(s.has_value());
    REQUIRE(s.value().positioned);
    CHECK(s.value().recno == 2u);
    CHECK(static_cast<unsigned char>(ix.current_key()[0]) == 0x9Du);

    s = ix.next();
    REQUIRE(s.has_value());
    REQUIRE(s.value().positioned);
    CHECK(s.value().recno == 3u);
    CHECK(ix.current_key().substr(0, 3) == "MAD");

    auto sk = ix.seek_key("M", true);
    REQUIRE(sk.has_value());
    CHECK(sk.value().hit == SeekHit::Exact);
    CHECK(sk.value().recno == 3u);

    fs::remove(p, ec);
}

TEST_CASE("CDX build_bulk without collation keeps binary byte order") {
    auto p = fs::temp_directory_path() / "openads_cdx_bulk_binary.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    auto created = CdxIndex::create(p.string(), "BYNAME", "NAME", 8,
                                    false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();

    std::vector<std::pair<std::string, std::uint32_t>> keys = {
        {pad8("ZBYDDDDD"), 4},
        {pad8("LACAAAAA"), 1},
        {pad8("\x9D""ABBBBBB"), 2},
        {pad8("MADCCCCC"), 3},
    };
    REQUIRE(ix.build_bulk(std::move(keys)).has_value());

    auto s = ix.seek_first();
    REQUIRE(s.has_value());
    CHECK(ix.current_key().substr(0, 3) == "LAC");

    s = ix.next();
    REQUIRE(s.has_value());
    CHECK(ix.current_key().substr(0, 3) == "MAD");

    s = ix.next();
    REQUIRE(s.has_value());
    CHECK(ix.current_key().substr(0, 3) == "ZBY");

    s = ix.next();
    REQUIRE(s.has_value());
    CHECK(static_cast<unsigned char>(ix.current_key()[0]) == 0x9Du);

    fs::remove(p, ec);
}

TEST_CASE("CDX soft seek under PL852 skips L-stroke for M prefix") {
    auto p = fs::temp_directory_path() / "openads_cdx_soft_pl852.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    const auto* coll = openads::engine::lookup_oem_collation("PL852");
    REQUIRE(coll != nullptr);

    auto created = CdxIndex::create(p.string(), "BYNAME", "NAME", 8,
                                    false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();
    ix.set_oem_sort_table(coll->sort);

    std::vector<std::pair<std::string, std::uint32_t>> keys = {
        {pad8("LACAAAAA"), 1},
        {pad8("\x9D""ABBBBBB"), 2},
        {pad8("MADCCCCC"), 3},
        {pad8("ZBYDDDDD"), 4},
    };
    REQUIRE(ix.build_bulk(std::move(keys)).has_value());

    auto sk = ix.seek_key("M", true);
    REQUIRE(sk.has_value());
    CHECK(sk.value().hit == SeekHit::Exact);
    CHECK(sk.value().recno == 3u);
    CHECK(ix.current_key().substr(0, 3) == "MAD");

    sk = ix.seek_key("Z", true);
    REQUIRE(sk.has_value());
    CHECK(sk.value().hit == SeekHit::Exact);
    CHECK(sk.value().recno == 4u);

    fs::remove(p, ec);
}

TEST_CASE("CDX insert after PL852 bulk keeps collation walk order") {
    auto p = fs::temp_directory_path() / "openads_cdx_ins_pl852.cdx";
    std::error_code ec;
    fs::remove(p, ec);

    const auto* coll = openads::engine::lookup_oem_collation("NTXPL852");
    REQUIRE(coll != nullptr);

    auto created = CdxIndex::create(p.string(), "BYNAME", "NAME", 8,
                                    false, false);
    REQUIRE(created.has_value());
    CdxIndex ix = std::move(created).value();
    ix.set_oem_sort_table(coll->sort);

    std::vector<std::pair<std::string, std::uint32_t>> keys = {
        {pad8("LACAAAAA"), 1},
        {pad8("MADCCCCC"), 3},
    };
    REQUIRE(ix.build_bulk(std::move(keys)).has_value());
    REQUIRE(ix.insert(2u, pad8("\x9D""ABBBBBB")).has_value());
    REQUIRE(ix.flush().has_value());

    auto s = ix.seek_first();
    REQUIRE(s.has_value());
    CHECK(s.value().recno == 1u);

    s = ix.next();
    REQUIRE(s.has_value());
    CHECK(s.value().recno == 2u);
    CHECK(static_cast<unsigned char>(ix.current_key()[0]) == 0x9Du);

    s = ix.next();
    REQUIRE(s.has_value());
    CHECK(s.value().recno == 3u);

    fs::remove(p, ec);
}