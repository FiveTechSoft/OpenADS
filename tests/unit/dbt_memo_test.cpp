#include "doctest.h"
#include "drivers/dbt/dbt_memo.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::MemoOpenMode;
using openads::drivers::dbt::DbtMemo;

TEST_CASE("DbtMemo round-trips a short memo through create + write + reopen") {
    auto p = fs::temp_directory_path() / "openads_m4_dbt_short.dbt";
    fs::remove(p);
    std::uint32_t block = 0;
    {
        auto created = DbtMemo::create(p.string());
        REQUIRE(created.has_value());
        DbtMemo m = std::move(created).value();
        auto w = m.write("hello world");
        REQUIRE(w.has_value());
        block = w.value();
        CHECK(block == 1);
        REQUIRE(m.flush().has_value());
    }
    {
        DbtMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == "hello world");
    }
    fs::remove(p);
}

TEST_CASE("DbtMemo round-trips a multi-block memo") {
    auto p = fs::temp_directory_path() / "openads_m4_dbt_multi.dbt";
    fs::remove(p);
    std::string payload(1500, 'x');
    payload.append("END");
    std::uint32_t block = 0;
    {
        auto created = DbtMemo::create(p.string());
        REQUIRE(created.has_value());
        DbtMemo m = std::move(created).value();
        auto w = m.write(payload);
        REQUIRE(w.has_value());
        block = w.value();
        REQUIRE(m.flush().has_value());
    }
    {
        DbtMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == payload);
    }
    fs::remove(p);
}

TEST_CASE("DbtMemo two consecutive writes return distinct blocks") {
    auto p = fs::temp_directory_path() / "openads_m4_dbt_two.dbt";
    fs::remove(p);
    std::uint32_t b1 = 0, b2 = 0;
    {
        auto created = DbtMemo::create(p.string());
        REQUIRE(created.has_value());
        DbtMemo m = std::move(created).value();
        auto w1 = m.write("first");
        REQUIRE(w1.has_value());
        b1 = w1.value();
        auto w2 = m.write("second longer payload");
        REQUIRE(w2.has_value());
        b2 = w2.value();
        REQUIRE(m.flush().has_value());
    }
    CHECK(b1 != b2);
    CHECK(b2 > b1);
    {
        DbtMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        auto r1 = m.read(b1);
        REQUIRE(r1.has_value());
        CHECK(r1.value() == "first");
        auto r2 = m.read(b2);
        REQUIRE(r2.has_value());
        CHECK(r2.value() == "second longer payload");
    }
    fs::remove(p);
}

TEST_CASE("DbtMemo read of block 0 returns empty string") {
    auto p = fs::temp_directory_path() / "openads_m4_dbt_zero.dbt";
    fs::remove(p);
    {
        auto created = DbtMemo::create(p.string());
        REQUIRE(created.has_value());
        DbtMemo m = std::move(created).value();
        auto r = m.read(0);
        REQUIRE(r.has_value());
        CHECK(r.value().empty());
    }
    fs::remove(p);
}

TEST_CASE("DbtMemo bounds multi-block growth before appending") {
    auto p = fs::temp_directory_path() / "openads_dbt_read_budget.dbt";
    fs::remove(p);
    {
        auto created = DbtMemo::create(p.string());
        REQUIRE(created.has_value());
        auto& memo = created.value();
        auto block = memo.write(std::string(1024, 'X'));
        REQUIRE(block.has_value());
        REQUIRE(memo.flush().has_value());
        memo.set_read_limit(1023);
        CHECK_FALSE(memo.read(block.value()).has_value());
        memo.set_read_limit(1024);
        auto result = memo.read(block.value());
        REQUIRE(result.has_value());
        CHECK(result.value().size() == 1024);
    }
    fs::remove(p);
}
