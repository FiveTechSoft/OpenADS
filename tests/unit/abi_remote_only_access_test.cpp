// OPENADS_REMOTE_ONLY_ACCESS=1: legacy local paths must never be
// accessed through ACE in a remote-only deployment -- a local
// AdsOpenTable / AdsCreateTable fails with AE_ACCESS_DENIED (the RDD
// turns it into a runtime error) instead of silently touching a local
// file. Legit local I/O goes through another RDD. (Pritpal Bedi.)
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// Sets an env var for the test's lifetime and clears it on exit (leak-proof
// across doctest's shared process, even if a REQUIRE throws).
struct EnvGuard {
    const char* name_;
    EnvGuard(const char* n, const char* v) : name_(n) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }
};

} // namespace

TEST_CASE("OPENADS_REMOTE_ONLY_ACCESS blocks local open/create, allows remote-mode flow") {
    const auto dir = fs::temp_directory_path() / "openads_remote_only_access";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);

    UNSIGNED8 name[64]   = "ro";
    UNSIGNED8 alias[64]  = "ro";
    UNSIGNED8 fields[64] = "ID,Numeric,4,0;NAME,Character,8";

    // Baseline, guard OFF: local create + open work.
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 64,
                           fields, &hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);

    // Guard ON: local open and create are both refused, and the
    // existing file must not be touched.
    {
        EnvGuard guard("OPENADS_REMOTE_ONLY_ACCESS", "1");
        hTable = 0;
        CHECK(AdsOpenTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 0,
                           &hTable) == openads::AE_ACCESS_DENIED);
        CHECK(hTable == 0);

        UNSIGNED8 name2[64] = "ro2";
        CHECK(AdsCreateTable(hConn, name2, alias, ADS_CDX, 0, 0, 0, 64,
                             fields, &hTable) == openads::AE_ACCESS_DENIED);
        CHECK(!fs::exists(dir / "ro2.dbf"));
    }

    // Guard OFF again (per-call env read): local open works.
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, name, alias, ADS_CDX, 0, 0, 0, 0, &hTable)
            == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);

    fs::remove_all(dir, ec);
}
