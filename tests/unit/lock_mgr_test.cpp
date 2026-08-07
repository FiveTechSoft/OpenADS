#include "doctest.h"
#include "engine/lock_mgr.h"
#include "platform/file.h"

#include <filesystem>

namespace fs = std::filesystem;
using openads::engine::LockMgr;
using openads::engine::LockingMode;
using openads::engine::TableTypeForLock;
using openads::platform::File;
using openads::platform::OpenMode;

TEST_CASE("LockMgr Compatible/NTX file lock uses Harbour Clipper FLock range") {
    auto p = fs::temp_directory_path() / "openads_m2_lock_ntx";
    fs::remove(p);
    {
        auto fres = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();
        LockMgr mgr;
        auto lock = mgr.lock_table_excl(f, TableTypeForLock::Ntx,
                                        LockingMode::Compatible);
        REQUIRE(lock.has_value());
        // Harbour DB_DBFLOCK_CLIPPER FLock: [1e9+1, +1e9) — covers every
        // record-lock byte so FLock vs RLock conflicts interoperate.
        CHECK(lock.value().offset() == 1'000'000'001ull);
        CHECK(lock.value().length() == 1'000'000'000ull);
    }
    fs::remove(p);
}

TEST_CASE("LockMgr Compatible/CDX record lock mirrors VFP physical position") {
    auto p = fs::temp_directory_path() / "openads_m2_lock_cdx";
    fs::remove(p);
    {
        auto fres = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();
        LockMgr mgr;
        // Harbour DB_DBFLOCK_VFP record lock: 0x40000000 + header_len +
        // (recno-1)*record_len. Geometry: 97-byte header, 41-byte records.
        mgr.set_record_geometry(97, 41);
        auto lock = mgr.lock_record_excl(f, TableTypeForLock::Cdx,
                                         LockingMode::Compatible, 7);
        REQUIRE(lock.has_value());
        CHECK(lock.value().offset() ==
              (0x40000000ull + 97ull + 6ull * 41ull));
        CHECK(lock.value().length() == 1ull);
    }
    fs::remove(p);
}

TEST_CASE("LockMgr re-entrant record lock keeps OS lock until final unlock") {
    auto p = fs::temp_directory_path() / "openads_m2_lock_reenter";
    fs::remove(p);
    {
        auto fres = File::open(p.string(), OpenMode::CreateRW);
        REQUIRE(fres.has_value());
        File f = std::move(fres).value();
        LockMgr mgr;
        auto a = mgr.lock_record_excl(f, TableTypeForLock::Cdx,
                                      LockingMode::Compatible, 42);
        REQUIRE(a.has_value());
        auto b = mgr.lock_record_excl(f, TableTypeForLock::Cdx,
                                      LockingMode::Compatible, 42);
        REQUIRE(b.has_value());
        CHECK_FALSE(mgr.unlock_record(f, TableTypeForLock::Cdx,
                                      LockingMode::Compatible, 42));
        CHECK(mgr.unlock_record(f, TableTypeForLock::Cdx,
                                LockingMode::Compatible, 42));
    }
    fs::remove(p);
}
