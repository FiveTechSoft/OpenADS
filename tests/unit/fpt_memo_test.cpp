#include "doctest.h"
#include "drivers/fpt/fpt_memo.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::MemoOpenMode;
using openads::drivers::fpt::FptMemo;

TEST_CASE("FptMemo round-trips a short memo through create+write+reopen (block=64)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_short.fpt";
    fs::remove(p);
    std::uint32_t block = 0;
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w = m.write("hello fpt");
        REQUIRE(w.has_value());
        block = w.value();
        REQUIRE(m.flush().has_value());
    }
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        CHECK(m.block_size() == 64);
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == "hello fpt");
    }
    fs::remove(p);
}

TEST_CASE("FptMemo round-trips a multi-block memo (block=64)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_multi.fpt";
    fs::remove(p);
    std::string payload(500, 'y');
    payload.append("END");
    std::uint32_t block = 0;
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w = m.write(payload);
        REQUIRE(w.has_value());
        block = w.value();
        REQUIRE(m.flush().has_value());
    }
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        auto r = m.read(block);
        REQUIRE(r.has_value());
        CHECK(r.value() == payload);
    }
    fs::remove(p);
}

TEST_CASE("FptMemo two writes with block=512 (FoxPro 2.x default)") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_512.fpt";
    fs::remove(p);
    std::uint32_t b1 = 0, b2 = 0;
    {
        auto created = FptMemo::create(p.string(), 512);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto w1 = m.write("first");
        REQUIRE(w1.has_value()); b1 = w1.value();
        auto w2 = m.write("second");
        REQUIRE(w2.has_value()); b2 = w2.value();
        REQUIRE(m.flush().has_value());
    }
    CHECK(b1 != b2);
    CHECK(b2 > b1);
    {
        FptMemo m;
        REQUIRE(m.open(p.string(), MemoOpenMode::ReadOnly).has_value());
        CHECK(m.block_size() == 512);
        auto r1 = m.read(b1);
        REQUIRE(r1.has_value());
        CHECK(r1.value() == "first");
        auto r2 = m.read(b2);
        REQUIRE(r2.has_value());
        CHECK(r2.value() == "second");
    }
    fs::remove(p);
}

TEST_CASE("FptMemo read of block 0 returns empty string") {
    auto p = fs::temp_directory_path() / "openads_m4_fpt_zero.fpt";
    fs::remove(p);
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        FptMemo m = std::move(created).value();
        auto r = m.read(0);
        REQUIRE(r.has_value());
        CHECK(r.value().empty());
    }
    fs::remove(p);
}

TEST_CASE("FptMemo read limit rejects before payload allocation and resets for local reads") {
    auto p = fs::temp_directory_path() / "openads_fpt_read_budget.fpt";
    fs::remove(p);
    {
        auto created = FptMemo::create(p.string(), 64);
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

TEST_CASE("FptMemo rejects forged length against file size before allocation") {
    auto p = fs::temp_directory_path() / "openads_fpt_forged_length.fpt";
    fs::remove(p);
    {
        auto created = FptMemo::create(p.string(), 64);
        REQUIRE(created.has_value());
        auto block = created.value().write("DATA");
        REQUIRE(block.has_value());
        REQUIRE(created.value().flush().has_value());
        std::fstream file(p, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(static_cast<std::streamoff>(block.value()) * 64 + 4);
        const unsigned char length[] = {0xff, 0xff, 0xff, 0xff};
        file.write(reinterpret_cast<const char*>(length), 4);
        file.flush();
        CHECK_FALSE(created.value().read(block.value()).has_value());
    }
    fs::remove(p);
}
