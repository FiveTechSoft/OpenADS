// tests/unit/abi_remote_append_meta_test.cpp
// mtfix68: a kCapAppendMetadata server's AppendBlankAck carries
// [u32 recno][u8 lock_held][u8 bof][u8 eof]. The client books the recno
// (mtfix67 certification) and the already-taken auto-lock (held_recs),
// so the following RecNo/RLOCK cost no round trips. Old-server acks stay
// empty and keep the legacy two-trip path.
#include "doctest.h"
#include "network/client.h"
#include "network/server.h"
#include "openads/ace.h"
#include "util/log.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::atomic<unsigned> g_frames{0};

void count_frames(const void* /*conn*/, std::uint8_t /*op*/, std::uint32_t /*tid*/,
                  std::size_t /*req_bytes*/, std::uint8_t /*rep_op*/,
                  std::size_t /*rep_bytes*/, long long /*us*/) {
    g_frames.fetch_add(1);
}

struct EnvGuard {
    const char* name; const char* val;
    EnvGuard(const char* n, const char* v) : name(n), val(v) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(name, "");
#else
        unsetenv(name);
#endif
    }
};

void seed_table(const fs::path& dir, const char* name, int rows) {
    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0;NM,C,10,0";
    UNSIGNED8 tn[16]{};
    std::memcpy(tn, name, std::strlen(name) + 1);
    ADSHANDLE hT0 = 0;
    REQUIRE(AdsCreateTable(hC0, tn, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
            == AE_SUCCESS);
    for (int i = 0; i < rows; ++i) {
        REQUIRE(AdsAppendRecord(hT0) == AE_SUCCESS);
        REQUIRE(AdsSetField(hT0, (UNSIGNED8*)"ID", (UNSIGNED8*)"7", 1) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT0) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);
}

struct Remote {
    openads::network::Server srv;
    ADSHANDLE hC = 0;
    fs::path dir;
    explicit Remote(const char* dirname) {
        dir = fs::temp_directory_path() / dirname;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        seed_table(dir, "AM.DBF", 3);
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        char uri[512]{};
        std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                      static_cast<unsigned>(srv.port()), dir.string().c_str());
        UNSIGNED8 sb[512]{};
        std::memcpy(sb, uri, std::strlen(uri) + 1);
        REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
                == AE_SUCCESS);
    }
    ~Remote() {
        if (hC) AdsDisconnect(hC);
        srv.stop();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("mtfix68: append ack trailer books recno + lock; RecNo/RLOCK go local") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    Remote R("openads_appendmeta");
    openads::network::set_frame_trace_hook(&count_frames);
    struct Unhook { ~Unhook() { openads::network::set_frame_trace_hook(nullptr); } } unhook;

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);

    g_frames.store(0);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    const unsigned append_frames = g_frames.load();
    REQUIRE(append_frames == 1);   // the append op itself

    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 4);
    CHECK(g_frames.load() == append_frames);            // no RecNo trip

    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames);            // no RLOCK trip

    // Re-entrant own RLOCK: same local success, still no trip.
    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames);

    // UnlockRecord invalidates: the next RLOCK pays the wire again.
    REQUIRE(AdsUnlockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 1);
    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 2);

    // UnlockTable invalidates: same.
    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 3);
    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 4);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
}

// Covers both compat directions with one kill-switch: the env stops the
// client's advertisement AND the server's send, which is wire-identical to
// old-client/new-server and new-client/old-server (no trailer either way).
TEST_CASE("mtfix68: old-peer empty ack keeps the legacy two-trip path") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    EnvGuard noenv("OPENADS_NO_APPEND_META_CAP", "1");   // server sends no trailer
    Remote R("openads_appendmeta_old");
    openads::network::set_frame_trace_hook(&count_frames);
    struct Unhook { ~Unhook() { openads::network::set_frame_trace_hook(nullptr); } } unhook;

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);

    g_frames.store(0);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    const unsigned append_frames = g_frames.load();
    REQUIRE(append_frames == 1);

    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 4);
    CHECK(g_frames.load() == append_frames + 1);        // legacy RecNo trip

    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 2);        // legacy RLOCK trip

    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
}

TEST_CASE("mtfix68: table-lock cover - trailer reports lock_held=0, RLOCK still pays wire") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    Remote R("openads_appendmeta_flock");
    openads::network::set_frame_trace_hook(&count_frames);
    struct Unhook { ~Unhook() { openads::network::set_frame_trace_hook(nullptr); } } unhook;

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsLockTable(hT) == AE_SUCCESS);

    g_frames.store(0);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    const unsigned append_frames = g_frames.load();
    REQUIRE(append_frames == 1);

    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rec) == AE_SUCCESS);
    CHECK(rec == 4);
    CHECK(g_frames.load() == append_frames);            // recno certified even under FLock

    // No auto-lock under whole-file cover: not booked; RLOCK pays the wire.
    REQUIRE(AdsLockRecord(hT, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == append_frames + 1);

    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
}

TEST_CASE("mtfix68: prior lock uncertainty on the table survives the trailer") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    // Locked-row off so LockRecord(0) below cannot come back with the row
    // (which would name the recno and keep the ledger certain).
    EnvGuard nolr("OPENADS_NO_LOCKED_ROW_CAP", "1");
    Remote R("openads_appendmeta_unc");
    openads::network::set_frame_trace_hook(&count_frames);
    struct Unhook { ~Unhook() { openads::network::set_frame_trace_hook(nullptr); } } unhook;

    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);

    // Manufacture real ledger uncertainty on a NEW server: append (trailer
    // books the blank, row cache stays cold), then LockRecord(0) = "current
    // record" with no resolvable recno client-side. The wire lock succeeds
    // (server locks the appended record again; repeated same-handle lock is
    // a success by design) but the client cannot name it -> uncertain.
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    UNSIGNED32 rec1 = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rec1) == AE_SUCCESS);
    CHECK(rec1 == 4);
    REQUIRE(AdsLockRecord(hT, 0) == AE_SUCCESS);

    // A second trailer append books ITS recno exactly, but must NOT clear
    // the uncertainty the first lock created: GetAllLocks still pays the
    // wire, while the new recno itself is served locally.
    g_frames.store(0);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(g_frames.load() == 1);
    UNSIGNED32 rec2 = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rec2) == AE_SUCCESS);
    CHECK(rec2 == 5);
    CHECK(g_frames.load() == 1);                          // recno stayed local

    UNSIGNED32 recs[8]{};
    UNSIGNED16 n = 8;
    REQUIRE(AdsGetAllLocks(hT, recs, &n) == AE_SUCCESS);
    CHECK(g_frames.load() == 2);                          // uncertain: wire
    CHECK(n >= 2u);

    REQUIRE(AdsUnlockTable(hT) == AE_SUCCESS);
    // After a full unlock the ledger is provably empty: local answer.
    const unsigned before = g_frames.load();
    n = 8;
    REQUIRE(AdsGetAllLocks(hT, recs, &n) == AE_SUCCESS);
    CHECK(g_frames.load() == before);
    CHECK(n == 0u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
}

TEST_CASE("mtfix68: two concurrent sessions each get their own appended recno") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    Remote R("openads_appendmeta_mtx");

    // Second connection to the SAME server + data dir.
    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(R.srv.port()), R.dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC2 = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC2)
            == AE_SUCCESS);

    ADSHANDLE hA = 0, hB = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hA)
            == AE_SUCCESS);
    REQUIRE(AdsOpenTable(hC2, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hB)
            == AE_SUCCESS);

    REQUIRE(AdsAppendRecord(hA) == AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hB) == AE_SUCCESS);
    UNSIGNED32 ra = 0, rb = 0;
    REQUIRE(AdsGetRecordNum(hA, 0, &ra) == AE_SUCCESS);   // local answers
    REQUIRE(AdsGetRecordNum(hB, 0, &rb) == AE_SUCCESS);
    CHECK(ra >= 4);
    CHECK(rb >= 4);
    CHECK(ra != rb);                                      // no cross-talk

    // Each session's local RLOCK serves only its own record.
    openads::network::set_frame_trace_hook(&count_frames);
    g_frames.store(0);
    REQUIRE(AdsLockRecord(hA, ra) == AE_SUCCESS);
    REQUIRE(AdsLockRecord(hB, rb) == AE_SUCCESS);
    CHECK(g_frames.load() == 0);
    openads::network::set_frame_trace_hook(nullptr);

    REQUIRE(AdsUnlockTable(hA) == AE_SUCCESS);
    REQUIRE(AdsUnlockTable(hB) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hA) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hB) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC2) == AE_SUCCESS);
}

TEST_CASE("mtfix68: failed append books nothing; close/reopen leaves no stale lock") {
    const bool old_logging = openads::util::logging_enabled();
    struct Restore { bool old; ~Restore() { openads::util::set_logging_enabled(old); } } restore{old_logging};
    openads::util::set_logging_enabled(true);
    Remote R("openads_appendmeta_fail");
    openads::network::set_frame_trace_hook(&count_frames);
    struct Unhook { ~Unhook() { openads::network::set_frame_trace_hook(nullptr); } } unhook;

    // Read-only open: the append must fail and book nothing.
    ADSHANDLE hRO = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX,
                         0, 0, 0, ADS_READONLY, &hRO) == AE_SUCCESS);
    CHECK(AdsAppendRecord(hRO) != AE_SUCCESS);
    UNSIGNED32 rec = 0;
    g_frames.store(0);
    if (AdsGetRecordNum(hRO, 0, &rec) == AE_SUCCESS) {
        REQUIRE(AdsLockRecord(hRO, rec) == AE_SUCCESS);
        CHECK(g_frames.load() >= 2);   // both paid the wire; nothing booked
        REQUIRE(AdsUnlockRecord(hRO, rec) == AE_SUCCESS);
    }
    REQUIRE(AdsCloseTable(hRO) == AE_SUCCESS);

    // Normal append, then close + reopen: the lock must not survive.
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(hT, 0, &rec) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    ADSHANDLE hT2 = 0;
    REQUIRE(AdsOpenTable(R.hC, (UNSIGNED8*)"AM.DBF", nullptr, ADS_CDX, 0, 0, 0, 0, &hT2)
            == AE_SUCCESS);
    g_frames.store(0);
    REQUIRE(AdsLockRecord(hT2, rec) == AE_SUCCESS);
    CHECK(g_frames.load() == 1);   // fresh handle: no stale local success
    REQUIRE(AdsUnlockRecord(hT2, rec) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT2) == AE_SUCCESS);
}
