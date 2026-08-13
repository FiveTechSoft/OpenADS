#include "doctest.h"
#include "engine/repl_queue.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
static void safe_remove(const fs::path& p) { std::error_code ec; fs::remove(p, ec); }
using openads::engine::ReplQueue;
using openads::engine::ReplRecType;
using openads::engine::ReplRecord;
using openads::engine::ReplIdent;

TEST_CASE("ReplQueue: append INSERT and read back") {
    auto p = fs::temp_directory_path() / "openads_replq.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord r;
    r.type = ReplRecType::Insert;
    r.tx_id = 0;
    r.source_table = "cust";
    r.identity.push_back({"ID", "1"});
    r.after = {1, 2, 3};
    auto lsn = q.append(r);
    REQUIRE(lsn.has_value());
    CHECK(lsn.value() == 1);
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 1);
    CHECK(recs.value().at(0).source_table == "cust");
    CHECK(recs.value().at(0).identity[0].name == "ID");
    CHECK(recs.value().at(0).identity[0].value == "1");
    CHECK(recs.value().at(0).after == std::vector<std::uint8_t>{1, 2, 3});
    safe_remove(p);
}

TEST_CASE("ReplQueue: append UPDATE with before and after") {
    auto p = fs::temp_directory_path() / "openads_replq_updr.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord r;
    r.type = ReplRecType::Update;
    r.tx_id = 5;
    r.source_table = "orders";
    r.identity.push_back({"ORDER_ID", "42"});
    r.before = {10, 20};
    r.after  = {30, 40};
    auto lsn = q.append(r);
    REQUIRE(lsn.has_value());
    CHECK(lsn.value() == 1);
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 1);
    CHECK(recs.value().at(0).type == ReplRecType::Update);
    CHECK(recs.value().at(0).before == std::vector<std::uint8_t>{10, 20});
    CHECK(recs.value().at(0).after  == std::vector<std::uint8_t>{30, 40});
    CHECK(recs.value().at(0).tx_id == 5);
    safe_remove(p);
}

TEST_CASE("ReplQueue: append DELETE with before image") {
    auto p = fs::temp_directory_path() / "openads_replq_del.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord r;
    r.type = ReplRecType::Delete;
    r.tx_id = 0;
    r.source_table = "items";
    r.identity.push_back({"SKU", "ABC"});
    r.before = {9, 8, 7};
    auto lsn = q.append(r);
    REQUIRE(lsn.has_value());
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 1);
    CHECK(recs.value().at(0).type == ReplRecType::Delete);
    CHECK(recs.value().at(0).before == std::vector<std::uint8_t>{9, 8, 7});
    CHECK(recs.value().at(0).after.empty());
    safe_remove(p);
}

TEST_CASE("ReplQueue: read_from skips already applied LSN") {
    auto p = fs::temp_directory_path() / "openads_replq_skip.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord r1;
    r1.type = ReplRecType::Insert;
    r1.source_table = "t1";
    r1.identity.push_back({"K", "1"});
    r1.after = {1};
    REQUIRE(q.append(r1).has_value());
    ReplRecord r2;
    r2.type = ReplRecType::Insert;
    r2.source_table = "t2";
    r2.identity.push_back({"K", "2"});
    r2.after = {2};
    REQUIRE(q.append(r2).has_value());
    auto recs = q.read_from(1);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 1);
    CHECK(recs.value().at(0).source_table == "t2");
    CHECK(recs.value().at(0).lsn == 2);
    safe_remove(p);
}

TEST_CASE("ReplQueue: append TX_BEGIN / TX_COMMIT / TX_ABORT") {
    auto p = fs::temp_directory_path() / "openads_replq_tx.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord rb; rb.type = ReplRecType::TxBegin;  rb.tx_id = 10;
    ReplRecord rc; rc.type = ReplRecType::TxCommit; rc.tx_id = 10;
    ReplRecord ra; ra.type = ReplRecType::TxAbort;  ra.tx_id = 11;
    auto l1 = q.append(rb);
    auto l2 = q.append(rc);
    auto l3 = q.append(ra);
    REQUIRE(l1.has_value());
    REQUIRE(l2.has_value());
    REQUIRE(l3.has_value());
    CHECK(l1.value() < l2.value());
    CHECK(l2.value() < l3.value());
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 3);
    CHECK(recs.value().at(0).type == ReplRecType::TxBegin);
    CHECK(recs.value().at(0).tx_id == 10);
    CHECK(recs.value().at(0).source_table.empty());
    CHECK(recs.value().at(1).type == ReplRecType::TxCommit);
    CHECK(recs.value().at(2).type == ReplRecType::TxAbort);
    safe_remove(p);
}

TEST_CASE("ReplQueue: multiple identity columns") {
    auto p = fs::temp_directory_path() / "openads_replq_mident.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    ReplRecord r;
    r.type = ReplRecType::Insert;
    r.source_table = "lineitem";
    r.identity.push_back({"ORDER_ID", "100"});
    r.identity.push_back({"LINE_NO", "3"});
    r.after = {5, 6, 7};
    REQUIRE(q.append(r).has_value());
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs.value().size() == 1);
    REQUIRE(recs.value().at(0).identity.size() == 2);
    CHECK(recs.value().at(0).identity[0].name == "ORDER_ID");
    CHECK(recs.value().at(0).identity[0].value == "100");
    CHECK(recs.value().at(0).identity[1].name == "LINE_NO");
    CHECK(recs.value().at(0).identity[1].value == "3");
    safe_remove(p);
}

TEST_CASE("ReplQueue: corrupt tail is ignored, prefix survives") {
    auto p = fs::temp_directory_path() / "openads_replq_corrupt.bin";
    safe_remove(p);
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "good";
        r.identity.push_back({"K", "1"});
        r.after = {10};
        REQUIRE(q.append(r).has_value());
    }
    // Append garbage bytes.
    {
        std::ofstream out(p, std::ios::binary | std::ios::app);
        char garbage[32] = {};
        garbage[0] = 'X'; garbage[1] = 'Y'; garbage[2] = 'Z';
        out.write(garbage, sizeof(garbage));
    }
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        auto recs = q.read_from(0);
        REQUIRE(recs.has_value());
        REQUIRE(recs.value().size() == 1);
        CHECK(recs.value().at(0).source_table == "good");
    }
    safe_remove(p);
}

TEST_CASE("ReplQueue: reopen resumes LSN counter") {
    auto p = fs::temp_directory_path() / "openads_replq_lsn.bin";
    safe_remove(p);
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "a";
        r.identity.push_back({"K", "1"});
        r.after = {1};
        REQUIRE(q.append(r).has_value());
        REQUIRE(q.append(r).has_value());
        REQUIRE(q.append(r).has_value());
        CHECK(q.high_water_lsn() == 4);
    }
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        CHECK(q.high_water_lsn() == 4);
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "b";
        r.identity.push_back({"K", "2"});
        r.after = {2};
        auto lsn = q.append(r);
        REQUIRE(lsn.has_value());
        CHECK(lsn.value() == 4);
    }
    safe_remove(p);
}

TEST_CASE("ReplQueue: empty file returns empty vector") {
    auto p = fs::temp_directory_path() / "openads_replq_empty.bin";
    safe_remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    CHECK(recs.value().size() == 0);
    CHECK(q.high_water_lsn() == 1);
    safe_remove(p);
}

TEST_CASE("ReplQueue: bad CRC stops at that record") {
    auto p = fs::temp_directory_path() / "openads_replq_crc.bin";
    safe_remove(p);
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "first";
        r.identity.push_back({"K", "1"});
        r.after = {1};
        REQUIRE(q.append(r).has_value());
        REQUIRE(q.append(r).has_value());
    }
    // Flip a byte in the second record's payload area.
    auto sz = fs::file_size(p);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
    {
        std::ifstream in(p, std::ios::binary);
        in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    // First record ends at RPLQ_HEADER_LEN + payload + 4. Flip the CRC of
    // the second record (last 4 bytes).
    if (bytes.size() >= 4) bytes[bytes.size() - 2] ^= 0xFFu;
    {
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    {
        ReplQueue q;
        REQUIRE(q.open(p.string()).has_value());
        auto recs = q.read_from(0);
        REQUIRE(recs.has_value());
        REQUIRE(recs.value().size() == 1);
        CHECK(recs.value().at(0).source_table == "first");
    }
    safe_remove(p);
}
