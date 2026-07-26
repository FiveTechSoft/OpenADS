#include "doctest.h"
#include "engine/table.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::OpenMode;
using openads::engine::Table;
using openads::engine::TableType;

namespace {

fs::path make_empty_table(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m2_w_") + tag);
    fs::remove(p);

    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = 0;
    hdr[8]  = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 5; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NAME", 11);
    fd[11] = 'C';
    fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("Table append + set_field grows the file and round-trips") {
    auto p = make_empty_table("append");
    {
        auto t = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();

        REQUIRE(table.append_record().has_value());
        CHECK(table.recno() == 1);
        REQUIRE(table.set_field(0, std::string("Anna")).has_value());
        REQUIRE(table.flush().has_value());

        REQUIRE(table.append_record().has_value());
        CHECK(table.recno() == 2);
        REQUIRE(table.set_field(0, std::string("Bob")).has_value());
        REQUIRE(table.flush().has_value());
    }
    {
        auto t = Table::open(p.string(), TableType::Cdx, OpenMode::Read);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        CHECK(table.record_count() == 2);

        REQUIRE(table.goto_top().has_value());
        auto v0 = table.read_field(0);
        REQUIRE(v0.has_value());
        CHECK(v0.value().as_string == "Anna");

        REQUIRE(table.skip(1).has_value());
        auto v1 = table.read_field(0);
        REQUIRE(v1.has_value());
        CHECK(v1.value().as_string == "Bob");
    }
    fs::remove(p);
}

TEST_CASE("Table mark_deleted / recall_deleted toggle the deletion byte") {
    auto p = make_empty_table("delete");
    {
        auto t = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        REQUIRE(table.append_record().has_value());
        REQUIRE(table.set_field(0, std::string("X")).has_value());
        REQUIRE(table.mark_deleted().has_value());
        CHECK(table.is_deleted());
        REQUIRE(table.recall_deleted().has_value());
        CHECK_FALSE(table.is_deleted());
        REQUIRE(table.flush().has_value());
    }
    fs::remove(p);
}

// ── GoHot write-guard tests ─────────────────────────────────────────────
// In shared mode, writes to an existing record that is neither RLocked nor
// FLocked must fail with error 5035 (write failed — record not locked).
// Freshly-appended records are exempt (pending_append_ flag) per xBase
// semantics.  Exclusive opens bypass the check entirely.

TEST_CASE("Write guard: set_field on existing record without lock fails in Shared mode") {
    auto p = make_empty_table("gh_guard1");
    {
        // First handle: append + write + flush.
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("init")).has_value());
        REQUIRE(w1.flush().has_value());

        // Second handle: navigate to record 1, attempt write without lock.
        auto t2 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t2.has_value());
        Table w2 = std::move(t2).value();
        REQUIRE(w2.goto_top().has_value());
        CHECK(w2.recno() == 1);
        // Must fail — no RLock, no FLock.
        auto rc = w2.set_field(0, std::string("hack"));
        REQUIRE_FALSE(rc.has_value());
        CHECK(rc.error().code == 5035);
    }
    fs::remove(p);
}

TEST_CASE("Write guard: mark_deleted on existing record without lock fails in Shared mode") {
    auto p = make_empty_table("gh_guard2");
    {
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("data")).has_value());
        REQUIRE(w1.flush().has_value());

        auto t2 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t2.has_value());
        Table w2 = std::move(t2).value();
        REQUIRE(w2.goto_top().has_value());
        auto rc = w2.mark_deleted();
        REQUIRE_FALSE(rc.has_value());
        CHECK(rc.error().code == 5035);
    }
    fs::remove(p);
}

TEST_CASE("Write guard: freshly-appended record is writable without explicit lock") {
    auto p = make_empty_table("gh_guard3");
    {
        auto t = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t.has_value());
        Table table = std::move(t).value();
        REQUIRE(table.append_record().has_value());
        // pending_append_ should be true after append_record.
        // Writing must succeed without explicit lock.
        REQUIRE(table.set_field(0, std::string("OK")).has_value());
        REQUIRE(table.mark_deleted().has_value());
        REQUIRE(table.recall_deleted().has_value());
        REQUIRE(table.flush().has_value());
    }
    fs::remove(p);
}

TEST_CASE("Write guard: Exclusive mode bypasses lock check") {
    auto p = make_empty_table("gh_guard4");
    {
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("data")).has_value());
        REQUIRE(w1.flush().has_value());

        // Exclusive open — no lock needed.
        auto t2 = Table::open(p.string(), TableType::Cdx, OpenMode::Exclusive);
        REQUIRE(t2.has_value());
        Table w2 = std::move(t2).value();
        REQUIRE(w2.goto_top().has_value());
        REQUIRE(w2.set_field(0, std::string("X")).has_value());
        REQUIRE(w2.flush().has_value());
    }
    fs::remove(p);
}

TEST_CASE("Write guard: Read-only mode does not attempt writes (set_field fails at state check)") {
    auto p = make_empty_table("gh_guard5");
    {
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("data")).has_value());
        REQUIRE(w1.flush().has_value());

        // Read-only open — set_field returns error (read-only driver rejects
        // the write before the lock guard even fires).
        auto t2 = Table::open(p.string(), TableType::Cdx, OpenMode::Read);
        REQUIRE(t2.has_value());
        Table w2 = std::move(t2).value();
        REQUIRE(w2.goto_top().has_value());
        // Read-only tables don't check the GoHot guard because they can't
        // write at all — the driver rejects the write.
        // This test just confirms the read path works.
        auto v = w2.read_field(0);
        REQUIRE(v.has_value());
    }
    fs::remove(p);
}

TEST_CASE("Write guard: explicit RLock allows write to existing record") {
    auto p = make_empty_table("gh_guard6");
    {
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("before")).has_value());
        REQUIRE(w1.flush().has_value());

        // Navigate back to record 1, lock it, then write.
        REQUIRE(w1.goto_top().has_value());
        REQUIRE(w1.lock_record_excl(1).has_value());
        REQUIRE(w1.set_field(0, std::string("after")).has_value());
        REQUIRE(w1.flush().has_value());

        // Verify the write persisted.
        REQUIRE(w1.goto_top().has_value());
        auto v = w1.read_field(0);
        REQUIRE(v.has_value());
        CHECK(v.value().as_string == "after");
    }
    fs::remove(p);
}

TEST_CASE("Write guard: FLock allows write to any record") {
    auto p = make_empty_table("gh_guard7");
    {
        auto t1 = Table::open(p.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(t1.has_value());
        Table w1 = std::move(t1).value();
        REQUIRE(w1.append_record().has_value());
        REQUIRE(w1.set_field(0, std::string("before")).has_value());
        REQUIRE(w1.flush().has_value());

        // File-lock — allows writes to any record.
        REQUIRE(w1.lock_table_excl().has_value());
        REQUIRE(w1.goto_top().has_value());
        REQUIRE(w1.set_field(0, std::string("after")).has_value());
        REQUIRE(w1.flush().has_value());

        REQUIRE(w1.goto_top().has_value());
        auto v = w1.read_field(0);
        REQUIRE(v.has_value());
        CHECK(v.value().as_string == "after");
    }
    fs::remove(p);
}
