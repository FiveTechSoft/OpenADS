// NTX inter-process index lock (Harbour DBFNTX DB_DBFLOCK_CLIPPER scheme):
// 1 byte at 1000000000 on the .ntx, exclusive for a write batch until
// flush(), shared around header refresh / cache rebuild, 16-bit header
// version bumped on every flushed batch so peers discard stale buffers.
#include "doctest.h"
#include "drivers/ntx/ntx_index.h"
#include "platform/file.h"
#include "platform/lock.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using openads::drivers::IndexOpenMode;
using openads::drivers::ntx::NtxIndex;
using openads::drivers::ntx::NTX_LOCK_OFF;

namespace {

fs::path fresh_ntx(const char* tag) {
    auto p = fs::temp_directory_path() /
             (std::string("openads_ntx_lock_") + tag + ".ntx");
    std::error_code ec;
    fs::remove(p, ec);
    auto c = NtxIndex::create(p.string(), "T", "NAME", 8, false, false);
    REQUIRE(c);
    return p;
}

std::uint16_t disk_version(const fs::path& p) {
    auto f = openads::platform::File::open(p.string(),
                                           openads::platform::OpenMode::ReadOnly);
    REQUIRE(f);
    std::uint8_t h[4]{};
    auto got = f.value().read_at(0, h, sizeof(h));
    REQUIRE(got);
    return static_cast<std::uint16_t>(h[2] | (h[3] << 8));
}

std::vector<std::uint32_t> all_recnos(NtxIndex& ix) {
    std::vector<std::uint32_t> out;
    auto s = ix.seek_first();
    while (s && s.value().positioned) {
        out.push_back(s.value().recno);
        s = ix.next();
    }
    return out;
}

void safe_remove(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

} // namespace

TEST_CASE("NTX lock: Harbour DBFNTX Clipper lock position") {
    CHECK(NTX_LOCK_OFF == 1000000000ULL);
}

TEST_CASE("NTX lock: write batch holds the lock until flush and bumps version") {
    auto p = fresh_ntx("batch");
    {
        NtxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared));
        const auto v0 = disk_version(p);
        CHECK(ix.header_version() == v0);
        CHECK_FALSE(ix.write_locked());
        REQUIRE(ix.insert(1, "ALPHA"));
        CHECK(ix.write_locked());
        REQUIRE(ix.insert(2, "BRAVO"));
        CHECK(ix.write_locked());
        REQUIRE(ix.flush());
        CHECK_FALSE(ix.write_locked());
        CHECK(disk_version(p) == static_cast<std::uint16_t>(v0 + 1));
        CHECK(ix.header_version() == disk_version(p));
        // A flush with nothing dirty writes nothing (no version bump).
        REQUIRE(ix.flush());
        CHECK(disk_version(p) == static_cast<std::uint16_t>(v0 + 1));
    }
    safe_remove(p);
}

TEST_CASE("NTX lock: a second handle sees a peer's flushed changes") {
    auto p = fresh_ntx("peer");
    {
        NtxIndex a, b;
        REQUIRE(a.open(p.string(), IndexOpenMode::Shared));
        REQUIRE(b.open(p.string(), IndexOpenMode::Shared));
        CHECK(all_recnos(b).empty());          // b caches the empty tree

        REQUIRE(a.insert(1, "ALPHA"));
        REQUIRE(a.insert(2, "BRAVO"));
        REQUIRE(a.flush());
        CHECK(all_recnos(b) == std::vector<std::uint32_t>{1, 2});

        // Writer b adopts a's tree before mutating, then a sees b's key.
        REQUIRE(b.insert(3, "CHARLIE"));
        REQUIRE(b.flush());
        CHECK(all_recnos(a) == std::vector<std::uint32_t>{1, 2, 3});

        // Enough keys to split pages, alternating writers.
        for (std::uint32_t r = 4; r <= 200; ++r) {
            NtxIndex& w = (r % 2) ? a : b;
            char key[16];
            std::snprintf(key, sizeof(key), "K%06u", r);
            REQUIRE(w.insert(r, key));
            REQUIRE(w.flush());
        }
        CHECK(all_recnos(a).size() == 200);
        CHECK(all_recnos(b).size() == 200);
    }
    {
        NtxIndex c;
        REQUIRE(c.open(p.string(), IndexOpenMode::Shared));
        CHECK(all_recnos(c).size() == 200);
    }
    safe_remove(p);
}

TEST_CASE("NTX lock: another thread's writer waits for the open batch") {
    auto p = fresh_ntx("thread");
    {
        NtxIndex a, b;
        REQUIRE(a.open(p.string(), IndexOpenMode::Shared));
        REQUIRE(b.open(p.string(), IndexOpenMode::Shared));
        REQUIRE(a.insert(1, "ALPHA"));          // batch open on this thread

        std::atomic<bool> done{false};
        std::atomic<bool> ok{false};
        std::thread t([&] {
            auto r = b.insert(2, "BRAVO");
            if (r) r = b.flush();
            ok   = static_cast<bool>(r);
            done = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK_FALSE(done.load());               // blocked behind a's batch
        REQUIRE(a.flush());
        t.join();
        CHECK(ok.load());
    }
    {
        NtxIndex c;
        REQUIRE(c.open(p.string(), IndexOpenMode::Shared));
        CHECK(all_recnos(c) == std::vector<std::uint32_t>{1, 2});
    }
    safe_remove(p);
}

#ifndef _WIN32
TEST_CASE("NTX lock: another process sees the OS byte lock during a batch") {
    auto p = fresh_ntx("proc");
    // Child exit codes: 0 = could lock the byte, 1 = locked by us, 2 = error.
    auto probe = [&](openads::platform::LockKind kind) {
        pid_t pid = ::fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            auto f = openads::platform::File::open(
                p.string(), openads::platform::OpenMode::ReadWrite);
            if (!f) ::_exit(2);
            auto l = openads::platform::ByteLock::try_acquire(
                f.value(), NTX_LOCK_OFF, 1, kind);
            ::_exit(l ? 0 : 1);
        }
        int st = 0;
        ::waitpid(pid, &st, 0);
        return WIFEXITED(st) ? WEXITSTATUS(st) : 3;
    };
    {
        NtxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared));
        CHECK(probe(openads::platform::LockKind::Exclusive) == 0);
        REQUIRE(ix.insert(1, "ALPHA"));
        CHECK(probe(openads::platform::LockKind::Exclusive) == 1);
        CHECK(probe(openads::platform::LockKind::Shared) == 1);   // readers wait
        REQUIRE(ix.flush());
        CHECK(probe(openads::platform::LockKind::Exclusive) == 0);
    }
    safe_remove(p);
}

TEST_CASE("NTX lock: two processes writing one .ntx keep a consistent tree") {
    auto p = fresh_ntx("stress");
    constexpr std::uint32_t kPerProc = 300;
    auto writer = [&](std::uint32_t first) -> bool {
        NtxIndex ix;
        if (!ix.open(p.string(), IndexOpenMode::Shared)) return false;
        for (std::uint32_t i = 0; i < kPerProc; ++i) {
            const std::uint32_t r = first + i * 2;
            char key[16];
            std::snprintf(key, sizeof(key), "K%06u", r);
            if (!ix.insert(r, key)) return false;
            if (!ix.flush()) return false;
        }
        return true;
    };
    pid_t pid = ::fork();
    REQUIRE(pid >= 0);
    if (pid == 0) ::_exit(writer(1) ? 0 : 1);
    const bool parent_ok = writer(2);
    int st = 0;
    ::waitpid(pid, &st, 0);
    CHECK(parent_ok);
    CHECK((WIFEXITED(st) && WEXITSTATUS(st) == 0));
    {
        NtxIndex c;
        REQUIRE(c.open(p.string(), IndexOpenMode::Shared));
        auto recs = all_recnos(c);
        REQUIRE(recs.size() == 2 * kPerProc);
        for (std::uint32_t i = 0; i < recs.size(); ++i) {
            CHECK(recs[i] == i + 1);            // keys K000001.. in order
        }
    }
    safe_remove(p);
}
#else
TEST_CASE("NTX lock: another handle sees the OS byte lock during a batch") {
    auto p = fresh_ntx("proc");
    auto probe = [&] {
        auto f = openads::platform::File::open(
            p.string(), openads::platform::OpenMode::ReadWrite);
        REQUIRE(f);
        auto l = openads::platform::ByteLock::try_acquire(
            f.value(), NTX_LOCK_OFF, 1, openads::platform::LockKind::Exclusive);
        return static_cast<bool>(l);
    };
    {
        NtxIndex ix;
        REQUIRE(ix.open(p.string(), IndexOpenMode::Shared));
        CHECK(probe());
        REQUIRE(ix.insert(1, "ALPHA"));
        CHECK_FALSE(probe());
        REQUIRE(ix.flush());
        CHECK(probe());
    }
    safe_remove(p);
}
#endif
