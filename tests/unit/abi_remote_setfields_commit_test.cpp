// Commit-boundary packet fusion. Pritpal Bedi's explicit key-only commit
// followed by a second whole-record commit remains two separate boundaries.
#include "doctest.h"
#include "abi/runtime.h"
#include "network/checked_record_flush.h"
#include "network/client.h"
#include "network/server.h"
#include "network/transport.h"
#include "openads/ace.h"
#include "util/log.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using namespace openads::network;
namespace fs = std::filesystem;
std::atomic<unsigned> commit_frames{0}, field_frames{0}, flush_frames{0}, probe_frames{0}, fetch_frames{0}, append_frames{0}, transition_frames{0};
void commit_trace(const void*, std::uint8_t op, std::uint32_t,
                  std::size_t, std::uint8_t, std::size_t bytes, long long) {
    if (op == 0x5E) { ++field_frames; if (bytes == 1) ++commit_frames; }
    if (op == 0x5A) ++flush_frames;
    if (op == 0x52) ++probe_frames;
    if (op == 0x50) ++append_frames;
    if (op == 0x9C) ++fetch_frames;
    if (op == 0x58 || op == 0x78 || op == 0x22) ++transition_frames;
}
struct CommitEnv {
    explicit CommitEnv(bool legacy) {
#ifdef _WIN32
        _putenv_s("OPENADS_NO_SETFIELDS_COMMIT_CAP", legacy ? "1" : "");
#else
        if (legacy) setenv("OPENADS_NO_SETFIELDS_COMMIT_CAP", "1", 1);
        else unsetenv("OPENADS_NO_SETFIELDS_COMMIT_CAP");
#endif
    }
    ~CommitEnv() {
#ifdef _WIN32
        _putenv_s("OPENADS_NO_SETFIELDS_COMMIT_CAP", "");
#else
        unsetenv("OPENADS_NO_SETFIELDS_COMMIT_CAP");
#endif
    }
};
struct CommitFixture {
    fs::path dir = fs::temp_directory_path() / "openads_setfields_commit";
    Server srv;
    ADSHANDLE conn = 0, table = 0;
    bool old_logging = openads::util::logging_enabled();
    CommitFixture() {
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
        ADSHANDLE local = 0, seed = 0;
        auto path = dir.string();
        REQUIRE(AdsConnect60((UNSIGNED8*)path.c_str(), ADS_LOCAL_SERVER,
                            nullptr, nullptr, 0, &local) == 0);
        REQUIRE(AdsCreateTable(local, (UNSIGNED8*)"CF.DBF", nullptr, ADS_CDX,
            0, 0, 0, 0, (UNSIGNED8*)"ID,N,8,0;NM,C,1000,0;AGE,N,3,0;DAY,D,8,0", &seed) == 0);
        REQUIRE(AdsCloseTable(seed) == 0); REQUIRE(AdsDisconnect(local) == 0);
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        auto uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;
        REQUIRE(AdsConnect60((UNSIGNED8*)uri.c_str(), ADS_REMOTE_SERVER,
                            nullptr, nullptr, 0, &conn) == 0);
        REQUIRE(AdsOpenTable(conn, (UNSIGNED8*)"CF.DBF", nullptr, ADS_CDX,
                            0, 0, 0, ADS_SHARED, &table) == 0);
        openads::util::set_logging_enabled(true);
        set_frame_trace_hook(&commit_trace);
        reset();
    }
    void reset() { commit_frames = 0; field_frames = 0; flush_frames = 0; probe_frames = 0; fetch_frames = 0; append_frames = 0; transition_frames = 0; }
    void put(const char* f, const std::string& v) {
        REQUIRE(AdsSetString(table, (UNSIGNED8*)f, (UNSIGNED8*)v.data(),
                            static_cast<UNSIGNED32>(v.size())) == 0);
    }
    void connect_peer(RemoteConnection& peer) {
        REQUIRE(peer.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    }
    ~CommitFixture() {
        set_frame_trace_hook(nullptr); openads::util::set_logging_enabled(old_logging);
        if (table) AdsCloseTable(table);
        if (conn) AdsDisconnect(conn);
        srv.stop();
        std::error_code ec; fs::remove_all(dir, ec);
    }
};
std::string trimmed(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}
}

TEST_CASE("commit fusion: one saved trip, two explicit commit boundaries, own locks") {
    for (bool legacy : {false, true}) {
        CAPTURE(legacy); CommitEnv env(legacy); CommitFixture f;
        RemoteConnection peer; f.connect_peer(peer);
        auto opened = peer.open_table("CF.DBF", 1); REQUIRE(opened.has_value());
        const auto id = opened.value().id;
        REQUIRE(AdsAppendRecord(f.table) == 0);
        f.reset(); f.put("ID", "71");
        CHECK(probe_frames.load() == 0); CHECK(field_frames.load() == 0);
        REQUIRE(AdsWriteRecord(f.table) == 0);  // key-only commit must NOT wait
        CHECK(field_frames.load() == 1);
        CHECK(flush_frames.load() == (legacy ? 1u : 0u));
        CHECK(commit_frames.load() == (legacy ? 0u : 1u));
        REQUIRE(peer.goto_record(id, 1).has_value());
        auto key = peer.get_field(id, "ID"); REQUIRE(key.has_value());
        CHECK(trimmed(key.value()) == "71");
        auto name = peer.get_field(id, "NM"); REQUIRE(name.has_value());
        CHECK(trimmed(name.value()).empty());
        // A second session cannot take the append owner's lock.
        CHECK_FALSE(peer.lock_record(id, 1).has_value());
        const std::string long_name(1000, 'x');
        f.reset(); f.put("NM", long_name); f.put("AGE", "42");
        CHECK(probe_frames.load() == 0); CHECK(field_frames.load() == 0);
        REQUIRE(AdsWriteRecord(f.table) == 0); // normal GoCold/commit entry
        CHECK(field_frames.load() == 1);
        CHECK(flush_frames.load() == (legacy ? 1u : 0u));
        CHECK(commit_frames.load() == (legacy ? 0u : 1u));
        f.reset(); REQUIRE(AdsWriteRecord(f.table) == 0); // clean commit
        CHECK(field_frames.load() == 0); CHECK(flush_frames.load() == 0);
        REQUIRE(peer.refresh_record(id).has_value());
        name = peer.get_field(id, "NM"); REQUIRE(name.has_value());
        CHECK(name.value() == long_name);
        auto age = peer.get_field(id, "AGE"); REQUIRE(age.has_value());
        CHECK(trimmed(age.value()) == "42");
        REQUIRE(AdsUnlockTable(f.table) == 0);
        REQUIRE(peer.lock_record(id, 1).has_value());
        REQUIRE(peer.unlock_record(id, 1).has_value());
    }
}

TEST_CASE("commit fusion: field error stops ordered prefix without committing it") {
    CommitEnv env(false); CommitFixture f;
    RemoteConnection direct; f.connect_peer(direct);
    auto opened = direct.open_table("CF.DBF", 1); REQUIRE(opened.has_value());
    auto id = opened.value().id;
    REQUIRE(direct.append_blank(id).has_value());
    f.reset();
    auto disk = [&] {
        std::ifstream file(f.dir / "CF.DBF", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto before = disk();
    auto result = direct.set_fields_batch(id,
        {{"ID", "71"}, {"BAD_FIELD", "no"}, {"AGE", "42"}}, true);
    CHECK(disk() == before); // failed apply did not run the commit flush
    CHECK_FALSE(result.has_value());
    CHECK(commit_frames.load() == 0); CHECK(flush_frames.load() == 0);
    auto key = direct.get_field(id, "ID"); REQUIRE(key.has_value());
    CHECK(trimmed(key.value()) == "71"); // already-applied prefix, no rollback
    auto age = direct.get_field(id, "AGE"); REQUIRE(age.has_value());
    CHECK(trimmed(age.value()) != "42"); // suffix not applied
    REQUIRE(direct.flush_table(id).has_value()); // caller chooses retry boundary
    REQUIRE(direct.unlock_record(id, 0).has_value());
}

namespace {
// Scripted wire peer: missing, malformed, dropped and explicit error ACKs.
class CommitReplyTransport : public ITransport {
    std::vector<std::uint8_t> incoming, outgoing;
    bool live = true;
public:
    int mode; unsigned set_requests = 0, flush_requests = 0;
    explicit CommitReplyTransport(int m) : mode(m) {}
    openads::util::Result<std::size_t> send(const std::uint8_t* data, std::size_t n) override {
        incoming.insert(incoming.end(), data, data + n);
        std::size_t used = 0; auto f = decode_frame(incoming.data(), incoming.size(), &used);
        if (!f) return n;
        Frame reply;
        switch (f.value().opcode) {
            case Opcode::Hello: reply.opcode = Opcode::HelloAck; break;
            case Opcode::Connect: {
                reply.opcode = Opcode::ConnectAck;
                std::string echo = "connected:fixture";
                reply.payload.assign(echo.begin(), echo.end());
                auto caps = kCapSetFieldsBatch | kCapSetFieldsCommit;
                for (int i = 0; i < 4; ++i) reply.payload.push_back(static_cast<std::uint8_t>(caps >> (8*i)));
                break;
            }
            case Opcode::SetFields:
                ++set_requests; reply.opcode = Opcode::SetFieldsAck;
                if (mode == 1) reply.payload = {1, 0};
                if (mode == 2) { incoming.clear(); return n; }
                if (mode == 3) { reply.opcode = Opcode::Error; reply.payload = {0xAB, 0x13, 0, 0}; }
                if (mode == 4) reply.payload = {1};
                if (mode == 5) { reply.opcode = Opcode::Error; reply.payload = {0x88, 0x13, 0, 0}; }
                break;
            case Opcode::FlushTable: ++flush_requests; reply.opcode = Opcode::FlushTableAck; break;
            default: reply.opcode = Opcode::Disconnect; break;
        }
        auto bytes = encode_frame(reply); if (bytes) outgoing = bytes.value();
        incoming.clear(); return n;
    }
    openads::util::Result<std::size_t> recv(std::uint8_t* data, std::size_t n) override {
        const auto count = std::min(n, outgoing.size());
        std::copy_n(outgoing.begin(), count, data); outgoing.erase(outgoing.begin(), outgoing.begin() + static_cast<std::ptrdiff_t>(count)); return count;
    }
    void close() noexcept override { live = false; }
    bool valid() const noexcept override { return live; }
};
}
TEST_CASE("commit fusion: ambiguous ACK poisons connection with no replay; server errors stay exact") {
    for (int mode : {0, 1, 2, 3, 4, 5}) {
        CAPTURE(mode); RemoteConnection conn;
        auto transport = std::make_unique<CommitReplyTransport>(mode); auto* t = transport.get();
        REQUIRE(conn.connect_with_transport(std::move(transport), "fixture").has_value());
        auto result = conn.set_fields_batch(7, {{"NM", "one"}}, true);
        CHECK(result.has_value() == (mode == 4));
        if (mode == 3) { CHECK(result.error().code == 5035); CHECK(conn.valid()); }
        else if (mode == 5) { CHECK(result.error().code == 5000); CHECK(conn.valid()); }
        else if (mode != 4) { CHECK_FALSE(conn.valid()); }
        CHECK(t->set_requests == 1); CHECK(t->flush_requests == 0);
    }
}

TEST_CASE("commit fusion: indexed append key order and raw numeric bytes survive") {
    CommitEnv env(false); CommitFixture f;
    ADSHANDLE index = 0;
    REQUIRE(AdsCreateIndex61(f.table, (UNSIGNED8*)"CF.CDX", (UNSIGNED8*)"BYID",
        (UNSIGNED8*)"STR(ID,8)", nullptr, nullptr, ADS_COMPOUND, 0, &index) == 0);
    for (int value : {3, 1, 2}) {
        REQUIRE(AdsAppendRecord(f.table) == 0);
        f.put("ID", std::to_string(value)); f.put("NM", "indexed");
        f.put("AGE", "42"); f.reset();
        REQUIRE(AdsWriteRecord(f.table) == 0);
        CHECK(commit_frames.load() == 1); CHECK(flush_frames.load() == 0);
        REQUIRE(AdsUnlockRecord(f.table, 0) == 0);
    }
    REQUIRE(AdsSetIndexOrderByHandle(f.table, index) == 0);
    REQUIRE(AdsGotoTop(f.table) == 0);
    for (unsigned rec : {2u, 3u, 1u}) {
        UNSIGNED32 got = 0;
        REQUIRE(AdsGetRecordNum(f.table, 0, &got) == 0); CHECK(got == rec);
        REQUIRE(AdsSkip(f.table, 1) == 0);
    }
    REQUIRE(AdsCloseTable(f.table) == 0); f.table = 0;
    std::ifstream file(f.dir / "CF.DBF", std::ios::binary);
    std::vector<unsigned char> head(32); file.read((char*)head.data(), 32);
    const unsigned hlen = head[8] | (head[9] << 8);
    const unsigned rlen = head[10] | (head[11] << 8);
    for (unsigned rec : {0u, 1u, 2u}) {
        file.seekg(static_cast<std::streamoff>(hlen + rec*rlen + 1 + 8 + 1000));
        char age[3]; file.read(age, 3); CHECK(std::string(age, 3) == " 42");
    }
}

TEST_CASE("append replacement batch: second append drains previous row and own reads avoid wire") {
    CommitEnv env(false); CommitFixture f;
    REQUIRE(AdsAppendRecord(f.table) == 0);
    f.reset(); f.put("NM", "first");
    CHECK(probe_frames.load() == 0);
    UNSIGNED8 value[32]{}; UNSIGNED32 length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    CHECK(trimmed(std::string((char*)value, length)) == "first");
    CHECK(fetch_frames.load() == 0);
    REQUIRE(AdsAppendRecord(f.table) == 0);
    CHECK(field_frames.load() == 1); CHECK(append_frames.load() == 1);
    CHECK(commit_frames.load() == 0);
    f.put("NM", "second"); REQUIRE(AdsWriteRecord(f.table) == 0);
    REQUIRE(AdsGotoRecord(f.table, 1) == 0);
    length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    CHECK(trimmed(std::string((char*)value, length)) == "first");
    REQUIRE(AdsGotoRecord(f.table, 2) == 0);
    length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    CHECK(trimmed(std::string((char*)value, length)) == "second");
}

TEST_CASE("append replacement batch: movement and unlock drain, existing-row probe stays") {
    CommitEnv env(false); CommitFixture f;
    REQUIRE(AdsAppendRecord(f.table) == 0);
    f.reset(); f.put("NM", "beforegoto");
    REQUIRE(AdsGotoRecord(f.table, 1) == 0);
    CHECK(probe_frames.load() == 0); CHECK(field_frames.load() == 1);
    CHECK(commit_frames.load() == 0); CHECK(flush_frames.load() == 0);
    // A real reposition ends append-context eligibility. Existing-row writes
    // retain their immediate first-field server probe.
    f.reset(); f.put("AGE", "12");
    CHECK(probe_frames.load() == 1);
    REQUIRE(AdsWriteRecord(f.table) == 0);
    REQUIRE(AdsUnlockTable(f.table) == 0);
    REQUIRE(AdsAppendRecord(f.table) == 0);
    f.reset(); f.put("NM", "unlockrow");
    REQUIRE(AdsUnlockTable(f.table) == 0);
    CHECK(probe_frames.load() == 0); CHECK(field_frames.load() == 1);
    CHECK(commit_frames.load() == 0); CHECK(flush_frames.load() == 0);
    // Check that the second row's replacement survived before releasing it.
    REQUIRE(AdsGotoRecord(f.table, 2) == 0);
    UNSIGNED8 value[32]{}; UNSIGNED32 length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    CHECK(trimmed(std::string((char*)value, length)) == "unlockrow");
}

TEST_CASE("append replacement batch: failed drain gates movement append unlock and close") {
    for (int transition : {0, 1, 2, 3}) {
        CAPTURE(transition); CommitEnv env(false); CommitFixture f;
        REQUIRE(AdsAppendRecord(f.table) == 0);
        f.put("NM", "prefix");
        auto& state = openads::abi::detail::state();
        auto* rt = state.registry.lookup<RemoteTable>(f.table,
            openads::session::HandleKind::RemoteTable);
        REQUIRE(rt != nullptr);
        // Inject a server-rejected second field after a valid applied prefix.
        // No production failure hook: this is a test-only pending-buffer fault.
        rt->pending_sets.emplace_back("BAD_FIELD", "no");
        f.reset();
        UNSIGNED32 rc = 0;
        switch (transition) {
        case 0: rc = AdsGotoRecord(f.table, 1); break;
        case 1: rc = AdsAppendRecord(f.table); break;
        case 2: rc = AdsUnlockTable(f.table); break;
        default: rc = AdsCloseTable(f.table); break;
        }
        REQUIRE(rc != 0); CHECK(field_frames.load() == 1);
        CHECK(append_frames.load() == 0); CHECK(transition_frames.load() == 0);
        REQUIRE(state.registry.lookup<RemoteTable>(f.table,
            openads::session::HandleKind::RemoteTable) == rt);
        CHECK(rt->held_recs.count(1) == 1);
        CHECK(rt->pending_sets.empty()); // disposition is partial, never auto-replay
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(f.table, 0, &rec) == 0); CHECK(rec == 1);
        RemoteConnection peer; f.connect_peer(peer);
        auto opened = peer.open_table("CF.DBF", 1); REQUIRE(opened.has_value());
        CHECK_FALSE(peer.lock_record(opened.value().id, 1).has_value());
        UNSIGNED32 count = 0;
        REQUIRE(AdsGetRecordCount(f.table, ADS_REFRESHCOUNT, &count) == 0);
        CHECK(count == 1);
    }
}

TEST_CASE("checked commit flush: twin error stops finish and original errors survive") {
    bool finished = false;
    auto failed = checked_record_flush([] { return 7010; }, [&]() -> openads::util::Result<void> {
        finished = true; return {};
    });
    CHECK_FALSE(failed.has_value()); CHECK(failed.error().code == 7010);
    CHECK_FALSE(finished);
    auto engine_failed = checked_record_flush([] { return 0; }, [&]() -> openads::util::Result<void> {
        finished = true; return openads::util::Error{7008, 0, "engine failure", ""};
    });
    CHECK(finished); CHECK_FALSE(engine_failed.has_value());
    CHECK(engine_failed.error().code == 7008);
    CHECK(checked_record_flush([] { return 0; }, []() -> openads::util::Result<void> { return {}; }).has_value());
}

TEST_CASE("append replacement overlay: numeric date string and immediate argument errors") {
    CommitEnv env(false); CommitFixture f;
    REQUIRE(AdsAppendRecord(f.table) == 0); f.reset();
    REQUIRE(AdsSetLong(f.table, (UNSIGNED8*)"AGE", 42) == 0);
    REQUIRE(AdsSetJulian(f.table, (UNSIGNED8*)"DAY", 2460000) == 0);
    f.put("NM", "overlay");
    CHECK(AdsSetString(f.table, (UNSIGNED8*)"BAD_FIELD", (UNSIGNED8*)"x", 1) != 0);
    SIGNED32 n = 0, day = 0; double d = 0;
    REQUIRE(AdsGetLong(f.table, (UNSIGNED8*)"AGE", &n) == 0); CHECK(n == 42);
    REQUIRE(AdsGetDouble(f.table, (UNSIGNED8*)"AGE", &d) == 0); CHECK(d == 42);
    REQUIRE(AdsGetJulian(f.table, (UNSIGNED8*)"DAY", &day) == 0); CHECK(day == 2460000);
    CHECK(AdsGetLong(f.table, (UNSIGNED8*)"AGE", nullptr) != 0);
    UNSIGNED8 value[1100]{}; UNSIGNED32 length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    const std::string before((char*)value, length);
    CHECK(trimmed(before) == "overlay"); CHECK(before.size() == 1000);
    CHECK(probe_frames.load() == 0); CHECK(field_frames.load() == 0);
    CHECK(fetch_frames.load() == 0);
    REQUIRE(AdsWriteRecord(f.table) == 0);
    length = sizeof(value);
    REQUIRE(AdsGetField(f.table, (UNSIGNED8*)"NM", value, &length, 0) == 0);
    CHECK(std::string((char*)value, length) == before);
    REQUIRE(AdsGetJulian(f.table, (UNSIGNED8*)"DAY", &day) == 0); CHECK(day == 2460000);
}

TEST_CASE("append replacement batch: lost ACK blocks transition and cannot replay") {
    for (int transition : {0, 1, 2, 3}) {
        CAPTURE(transition); CommitEnv env(false); CommitFixture f;
        REQUIRE(AdsAppendRecord(f.table) == 0); f.put("NM", "uncertain");
        auto& state = openads::abi::detail::state();
        auto* rt = state.registry.lookup<RemoteTable>(f.table,
            openads::session::HandleKind::RemoteTable);
        REQUIRE(rt != nullptr);
        RemoteConnection injected;
        auto transport = std::make_unique<CommitReplyTransport>(2);
        auto* observed = transport.get();
        REQUIRE(injected.connect_with_transport(std::move(transport), "fixture").has_value());
        auto* original = rt->conn;
        struct Restore { RemoteTable* rt; RemoteConnection* conn;
            ~Restore() { rt->conn = conn; } } restore{rt, original};
        rt->conn = &injected;
        UNSIGNED32 rc = 0;
        switch (transition) {
        case 0: rc = AdsGotoRecord(f.table, 1); break;
        case 1: rc = AdsAppendRecord(f.table); break;
        case 2: rc = AdsUnlockTable(f.table); break;
        default: rc = AdsCloseTable(f.table); break;
        }
        REQUIRE(rc != 0); CHECK_FALSE(injected.valid());
        CHECK(observed->set_requests == 1); CHECK(observed->flush_requests == 0);
        REQUIRE(state.registry.lookup<RemoteTable>(f.table,
            openads::session::HandleKind::RemoteTable) == rt);
        CHECK(rt->held_recs.count(1) == 1);
        CHECK(rt->pending_sets.empty());
        CHECK(AdsWriteRecord(f.table) != 0);
        CHECK(observed->set_requests == 1); CHECK(observed->flush_requests == 0);
    }
}

TEST_CASE("pending field drain extension: clean no-op and no forced commit or movement") {
    CommitEnv env(false); CommitFixture f;
    f.reset(); REQUIRE(AdsFlushPendingFields(f.table) == 0);
    CHECK(field_frames.load() == 0); CHECK(flush_frames.load() == 0);
    CHECK(append_frames.load() == 0); CHECK(transition_frames.load() == 0);
    REQUIRE(AdsAppendRecord(f.table) == 0);
    f.reset(); f.put("NM", "drain-only");
    REQUIRE(AdsFlushPendingFields(f.table) == 0);
    CHECK(field_frames.load() == 1); CHECK(commit_frames.load() == 0);
    CHECK(flush_frames.load() == 0); CHECK(transition_frames.load() == 0);
    CHECK(append_frames.load() == 0);
    f.reset(); REQUIRE(AdsFlushPendingFields(f.table) == 0);
    CHECK(field_frames.load() == 0); CHECK(flush_frames.load() == 0);
    CHECK(AdsFlushPendingFields(0) != 0);
}
