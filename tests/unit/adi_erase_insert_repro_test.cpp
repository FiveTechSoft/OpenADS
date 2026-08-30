#include "doctest.h"
#include "openads/ace.h"
#include <cstdio>
#include "drivers/adi/adi_index.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Isolated v1 repro: a legacy (key_len=0, keys re-derived from the LIVE
// record) ADI tag must erase an entry by recno even when the record bytes
// already hold the NEW key — the engine writes the record before syncing
// indexes, so at erase time key_for_recno_ no longer matches the old key.
// Before the fix this returned 5044, the engine tolerated it (append
// window) and the following insert duplicated the recno in the bag.
TEST_CASE("ADI erase then insert does not duplicate the new key") {
    auto dir = fs::temp_directory_path() / "oads_adi_erase_insert";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto bag = (dir / "t.adi").string();
    const auto tbl = (dir / "t.adt").string();

    // Real ADT + legacy v1 tag via the ABI (AdsCreateIndex61 defaults to
    // v1: key_in_leaf_=false, keys derived live from the record).
    {
        UNSIGNED8 srv[512] = {};
        const auto sp = dir.string();
        std::memcpy(srv, sp.c_str(), sp.size() + 1);
        ADSHANDLE hC = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC) == 0);
        UNSIGNED8 tname[] = "t.adt";
        UNSIGNED8 flds[] = "Tag,Character,12";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hC, tname, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                               flds, &hT) == 0);
        ADSHANDLE hI = 0;
        UNSIGNED8 idxf[] = "t.adi";
        REQUIRE(AdsCreateIndex61(hT, idxf, (UNSIGNED8*)"TAG", (UNSIGNED8*)"Tag",
                                 nullptr, nullptr, 0, 0, &hI) == 0);
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"Tag", (UNSIGNED8*)"R0000", 5) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
        REQUIRE(AdsCloseTable(hT) == 0);
        REQUIRE(AdsDisconnect(hC) == 0);
    }

    // Move the key UNDER the index's feet: rewrite the record bytes on disk
    // (R0000 -> R0001) without any index sync, mimicking the engine's
    // writeback-before-sync commit order.
    {
        FILE* f = std::fopen(tbl.c_str(), "r+b");
        REQUIRE(f != nullptr);
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<char> bytes(static_cast<std::size_t>(sz));
        REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
        const std::string oldk = "R0000";
        auto it = std::search(bytes.begin(), bytes.end(),
                              oldk.begin(), oldk.end());
        REQUIRE(it != bytes.end());
        std::memcpy(&*it, "R0001", 5);
        std::fseek(f, 0, SEEK_SET);
        REQUIRE(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
        std::fclose(f);
    }

    // Driver-level: erase by the OLD key must still find the entry (its
    // live key is now R0001), then the re-insert must not duplicate.
    {
        openads::drivers::adi::AdiIndex idx;
        auto of = idx.open_named(bag,
                                 openads::drivers::IndexOpenMode::Shared,
                                 "TAG");
        REQUIRE(static_cast<bool>(of));
        auto er = idx.erase(1, "R0000");
        CHECK(er.has_value());
        auto i2 = idx.insert(1, "R0001");
        REQUIRE(i2.has_value());
        (void)idx.flush();
    }

    std::vector<std::pair<std::string, std::uint32_t>> all;
    // Walk with a FRESH instance (the writer's own cursor cache could mask
    // an encode/decode asymmetry).
    {
        openads::drivers::adi::AdiIndex fresh;
        auto of = fresh.open_named(bag,
                                   openads::drivers::IndexOpenMode::Shared,
                                   "TAG");
        REQUIRE(static_cast<bool>(of));
        auto sk = fresh.seek_first();
        while (sk && sk.value().positioned) {
            all.emplace_back(fresh.current_key(), sk.value().recno);
            sk = fresh.next();
        }
    }
    std::string dump;
    for (auto& kv : all)
        dump += " [" + kv.first + "|" + std::to_string(kv.second) + "]";
    MESSAGE("walk:", dump);
    CHECK(all.size() == 1);
    if (all.size() == 1) {
        CHECK(all[0].second == 1u);
        CHECK(all[0].first.substr(0, 5) == "R0001");
    }
    fs::remove_all(dir, ec);
}

// Forensic page dump of the AdsCreateIndex61-produced bag after a key move
// (the shape from abi_index_intensive2's DIAG: append, set, write, edit,
// write). Pages are 512 bytes; level=u16@0, count=u16@2.
TEST_CASE("ADI page dump after engine key move") {
    auto dir = fs::temp_directory_path() / "oads_adi_pagedump";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Build table + index via the ABI (same path as the failing DIAG).
    UNSIGNED8 srv[512] = {};
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC) == 0);
    UNSIGNED8 tname[] = "t.adt";
    UNSIGNED8 flds[] = "Tag,Character,12";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hC, tname, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flds, &hT) == 0);
    ADSHANDLE hI = 0;
    UNSIGNED8 idxf[] = "t.adi";
    REQUIRE(AdsCreateIndex61(hT, idxf, (UNSIGNED8*)"TAG", (UNSIGNED8*)"Tag",
                             nullptr, nullptr, 0, 0, &hI) == 0);
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"Tag", (UNSIGNED8*)"R0000", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, (UNSIGNED8*)"Tag", (UNSIGNED8*)"R0001", 5) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hC) == 0);

    FILE* f = std::fopen((dir / "t.adi").string().c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    REQUIRE(std::fread(buf.data(), 1, buf.size(), f) == buf.size());
    std::fclose(f);

    std::string out;
    const std::size_t PS = 512;
    for (std::size_t off = 0; off + PS <= buf.size(); off += PS) {
        const std::uint8_t* pg = buf.data() + off;
        unsigned lv = pg[0] | (pg[1] << 8);
        unsigned ct = pg[2] | (pg[3] << 8);
        if (lv == 0 && ct == 0) continue;
        char b[128];
        std::snprintf(b, sizeof(b), " [pg%zu lv=%u ct=%u first='%.12s']",
                      off / PS, lv, ct, (const char*)(pg + 16));
        out += b;
    }
    MESSAGE("pages:", out);
    auto hexrange = [&](std::size_t pgno) {
        const std::uint8_t* pg = buf.data() + pgno * PS;
        std::string h;
        char b[8];
        for (std::size_t i = 0; i < 96; ++i) {
            std::snprintf(b, sizeof(b), "%02X", pg[24 + i]);
            h += b;
            if (i % 16 == 15) h += " ";
        }
        return h;
    };
    MESSAGE("pg2 raw:", hexrange(2));
    MESSAGE("pg5 raw:", hexrange(5));

    // The bag must hold exactly ONE entry for recno 1 (key moved R0000 ->
    // R0001): before the fix the tolerated failed erase left the stale
    // entry behind and the walk showed [R0001|1, R0001|1].
    std::vector<std::pair<std::string, std::uint32_t>> all;
    {
        openads::drivers::adi::AdiIndex fresh;
        auto of = fresh.open_named((dir / "t.adi").string(),
                                   openads::drivers::IndexOpenMode::Shared,
                                   "TAG");
        REQUIRE(static_cast<bool>(of));
        auto sk = fresh.seek_first();
        while (sk && sk.value().positioned) {
            all.emplace_back(fresh.current_key(), sk.value().recno);
            sk = fresh.next();
        }
    }
    std::string dump;
    for (auto& kv : all)
        dump += " [" + kv.first + "|" + std::to_string(kv.second) + "]";
    MESSAGE("walk:", dump);
    CHECK(all.size() == 1);
    fs::remove_all(dir, ec);
}
