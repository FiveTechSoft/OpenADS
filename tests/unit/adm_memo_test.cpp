#include "doctest.h"
#include "drivers/adm/adm_memo.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::drivers::adm::AdmMemo;
using openads::drivers::MemoOpenMode;

namespace {

fs::path adm_path(const char* tag) {
    return fs::temp_directory_path() /
           (std::string("openads_adm_") + tag + ".adm");
}

void safe_remove(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

TEST_CASE("AdmMemo: create then open yields consistent state") {
    auto p = adm_path("create");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    CHECK(created.value().block_size() == AdmMemo::kBlockSize);
    created.value().flush();
    created = AdmMemo{};  // close before reopen

    AdmMemo memo2;
    REQUIRE(memo2.open(p.string(), MemoOpenMode::Shared));
    CHECK(memo2.block_size() == AdmMemo::kBlockSize);
    memo2.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: write returns block number starting at kDataBlockOrigin") {
    auto p = adm_path("write1");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto& memo = created.value();

    auto res = memo.write("Hello, ADM!");
    REQUIRE(res);
    CHECK(res.value() == AdmMemo::kDataBlockOrigin);

    // "Hello, ADM!" = 12 bytes -> ceil(12/8) = 2 blocks
    auto res2 = memo.write("Second block");
    REQUIRE(res2);
    CHECK(res2.value() == AdmMemo::kDataBlockOrigin + 2);
    memo.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: write then read round-trip") {
    auto p = adm_path("rw");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto& memo = created.value();

    std::string payload = "Test memo content here";
    auto block = memo.write(payload);
    REQUIRE(block);

    auto readback = memo.read(block.value(),
                              static_cast<std::uint32_t>(payload.size()));
    REQUIRE(readback);
    CHECK(readback.value() == payload);
    memo.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: multiple writes allocate consecutive blocks") {
    auto p = adm_path("multi");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto& memo = created.value();

    // "First"=5 bytes->1 block, "Second"=6 bytes->1 block, "Third"=5 bytes->1 block
    auto b1 = memo.write("First");
    auto b2 = memo.write("Second");
    auto b3 = memo.write("Third");
    REQUIRE(b1); REQUIRE(b2); REQUIRE(b3);
    CHECK(b1.value() == AdmMemo::kDataBlockOrigin);
    CHECK(b2.value() == b1.value() + 1);
    CHECK(b3.value() == b2.value() + 1);
    memo.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: read block 0 returns empty") {
    auto p = adm_path("blk0");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto readback = created.value().read(0);
    REQUIRE(readback);
    CHECK(readback.value().empty());
    safe_remove(p);
}

TEST_CASE("AdmMemo: read block 0 with data_len returns empty") {
    auto p = adm_path("blk0len");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto readback = created.value().read(0, 10);
    REQUIRE(readback);
    CHECK(readback.value().empty());
    safe_remove(p);
}

TEST_CASE("AdmMemo: read with data_len=0 returns empty") {
    auto p = adm_path("rlen0");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto b = created.value().write("data");
    REQUIRE(b);
    auto readback = created.value().read(b.value(), 0);
    REQUIRE(readback);
    CHECK(readback.value().empty());
    created.value().flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: write to read-only fails") {
    auto p = adm_path("ro");
    safe_remove(p);
    {
        auto created = AdmMemo::create(p.string());
        REQUIRE(created);
        created.value().flush();
    }

    AdmMemo ro;
    REQUIRE(ro.open(p.string(), MemoOpenMode::ReadOnly));
    auto res = ro.write("should fail");
    CHECK_FALSE(res);
    safe_remove(p);
}

TEST_CASE("AdmMemo: free_block is a no-op (always succeeds)") {
    auto p = adm_path("free");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    REQUIRE(created.value().free_block(42));
    safe_remove(p);
}

TEST_CASE("AdmMemo: flush succeeds on fresh file") {
    auto p = adm_path("flush");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    REQUIRE(created.value().flush());
    safe_remove(p);
}

TEST_CASE("AdmMemo: large payload round-trip") {
    auto p = adm_path("large");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto& memo = created.value();

    std::string payload(100, 'X');
    auto block = memo.write(payload);
    REQUIRE(block);
    auto readback = memo.read(block.value(),
                              static_cast<std::uint32_t>(payload.size()));
    REQUIRE(readback);
    CHECK(readback.value() == payload);
    memo.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: empty payload round-trip") {
    auto p = adm_path("empty");
    safe_remove(p);
    auto created = AdmMemo::create(p.string());
    REQUIRE(created);
    auto& memo = created.value();

    auto block = memo.write("");
    REQUIRE(block);
    auto readback = memo.read(block.value(), 0);
    REQUIRE(readback);
    CHECK(readback.value().empty());
    memo.flush();
    safe_remove(p);
}

TEST_CASE("AdmMemo: open non-existent file fails") {
    AdmMemo memo;
    CHECK_FALSE(memo.open("/nonexistent/path/file.adm", MemoOpenMode::ReadOnly));
}

TEST_CASE("AdmMemo rejects oversized declared length before allocating") {
    auto p = adm_path("read_budget");
    safe_remove(p);
    {
        auto created = AdmMemo::create(p.string());
        REQUIRE(created.has_value());
        auto& memo = created.value();
        auto block = memo.write("DATA");
        REQUIRE(block.has_value());
        REQUIRE(memo.flush().has_value());
        memo.set_read_limit(3);
        CHECK_FALSE(memo.read(block.value(), 4).has_value());
        memo.set_read_limit(4);
        REQUIRE(memo.read(block.value(), 4).has_value());
        // Invalid disk reference must not allocate UINT32_MAX bytes locally either.
        memo.set_read_limit(std::numeric_limits<std::size_t>::max());
        CHECK_FALSE(memo.read(block.value(), UINT32_MAX).has_value());
    }
    safe_remove(p);
}
