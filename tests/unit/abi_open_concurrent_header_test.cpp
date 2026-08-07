#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// F5 — opening a CDX table must not fail just because another connection
// is mid-append. AdsAppendRecord holds an EXCLUSIVE byte-lock on the DBF
// header (offset 0..31) while it bumps the record count; on Windows a
// ReadFile over a region another handle locked exclusively returns
// ERROR_LOCK_VIOLATION (33), so AdsOpenTable's unlocked header read used
// to fail intermittently under concurrent writers. The driver now takes
// a SHARED lock (retried with back-off) before the header read, so open
// waits for the in-flight append instead of erroring.
//
// The failure is specific to Windows mandatory byte-range locking: POSIX
// fcntl locks are advisory and don't block reads, and locks held by the
// same process never conflict — so the reproduction is Windows-only.
#ifdef _WIN32

#include "platform/file.h"
#include "platform/lock.h"

#include <atomic>
#include <chrono>
#include <thread>

TEST_CASE("AdsOpenTable retries past an exclusive header lock (no lock-violation)") {
    auto dir = fs::temp_directory_path() / "openads_open_hdr_lock";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    // Create the table and close it so the .dbf exists on disk.
    {
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);
        UNSIGNED8 def[]   = "ID,N,6,0;NAME,C,10,0";
        UNSIGNED8 tname[] = "acct";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    const auto dbf = (dir / "acct.dbf").string();

    // Hold an exclusive lock on the header lock byte, mimicking an
    // in-flight append. The append serialisation point is Harbour's
    // DB_DBFLOCK_VFP header position (0x40000000, 1 byte).
    auto lf = openads::platform::File::open(
        dbf, openads::platform::OpenMode::OpenExisting);
    REQUIRE(static_cast<bool>(lf));
    auto file = std::move(lf).value();
    auto lk = openads::platform::ByteLock::try_acquire(
        file, 0x40000000ull, 1, openads::platform::LockKind::Exclusive);
    REQUIRE(static_cast<bool>(lk));

    // Open the table from another connection while the lock is held.
    std::atomic<UNSIGNED32> open_rc{0xFFFFFFFFu};
    std::thread opener([&] {
        ADSHANDLE hConn = 0;
        if (AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) != 0) {
            open_rc.store(0xFFFFFFFEu);
            return;
        }
        UNSIGNED8 leaf[64] = "acct.dbf";
        ADSHANDLE hT = 0;
        UNSIGNED32 rc = AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                                     0, 0, 0, 0, &hT);
        open_rc.store(rc);
        if (rc == 0) AdsCloseTable(hT);
        AdsDisconnect(hConn);
    });

    // Keep the header locked long enough that the opener is forced to
    // retry, then release. With the fix the open then succeeds; without
    // it the open already failed on the locked read.
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    (void)lk.value().release();

    opener.join();
    CHECK(open_rc.load() == 0u);

    fs::remove_all(dir, ec);
}

#endif // _WIN32
