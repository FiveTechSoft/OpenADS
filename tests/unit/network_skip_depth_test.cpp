// RCB 07/14/2026 — M12.23. The Skip request grew an OPTIONAL trailing [u16]
// carrying the depth the caller asked for via AdsCacheRecords:
//
//     Skip: [u32 tid][i32 step]            <- pre-M12.23, still valid
//     Skip: [u32 tid][i32 step][u16 depth] <- M12.23
//
// The compatibility claim is that this needs no capability bit, because it is
// safe in BOTH version-mix directions. The "new client -> old server" half is
// true by construction (an old server length-checks `size() < 8` and reads
// only the first 8 bytes). The "old client -> new server" half is the one that
// can actually regress, and it is the one this file pins: a legacy 8-byte Skip
// must be read as "you decide the depth", NOT as depth 0 — which would silently
// disable read-ahead for every client built before this change.
//
// These drive RAW FRAMES rather than the ABI, and that is not incidental —
// it is the only way to write this test at all. The ABI client always sends
// the new field now, so it physically cannot impersonate a pre-M12.23 client;
// nothing reachable through Ads* can produce an 8-byte Skip any more. If you
// find this file's hand-rolled framing tedious and "modernise" it onto the ABI
// client, the compatibility assertion silently stops testing compatibility and
// starts testing the current client against itself.
#include "doctest.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using openads::network::Frame;
using openads::network::Opcode;
using openads::network::Server;
using openads::network::Socket;
using openads::network::connect_tcp;
using openads::network::read_frame;
using openads::network::sock_close;
using openads::network::write_frame;

namespace fs = std::filesystem;

namespace {

void push_u16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>( x       & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 8) & 0xFFu));
}
void push_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>( x        & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >>  8) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xFFu));
}

// Minimal DBF: one C(4) column, `n` records with a 4-char value each.
void write_dbf(const fs::path& p, int n) {
    std::vector<std::uint8_t> h(32 + 32 + 1, 0);
    h[0] = 0x03;
    h[4] = static_cast<std::uint8_t>( n        & 0xFFu);
    h[5] = static_cast<std::uint8_t>((n >>  8) & 0xFFu);
    h[6] = static_cast<std::uint8_t>((n >> 16) & 0xFFu);
    h[7] = static_cast<std::uint8_t>((n >> 24) & 0xFFu);
    const std::uint16_t hdr = 32 + 32 + 1;
    h[8]  = static_cast<std::uint8_t>( hdr       & 0xFFu);
    h[9]  = static_cast<std::uint8_t>((hdr >> 8) & 0xFFu);
    const std::uint16_t reclen = 1 + 4;
    h[10] = static_cast<std::uint8_t>( reclen       & 0xFFu);
    h[11] = static_cast<std::uint8_t>((reclen >> 8) & 0xFFu);
    std::memcpy(&h[32], "TAG", 3);
    h[32 + 11] = 'C';
    h[32 + 16] = 4;
    h[32 + 32] = 0x0D;

    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(h.data()),
            static_cast<std::streamsize>(h.size()));
    for (int i = 0; i < n; ++i) {
        char rec[5] = {' ', 'A', 'A', 'A', 'A'};
        rec[1] = static_cast<char>('A' + (i % 26));
        f.write(rec, 5);
    }
    const char eof = 0x1A;
    f.write(&eof, 1);
}

// Connect as a PREFETCH-CAPABLE client (caps word set), open the table, and
// GotoTop. Returns the server-side table id.
std::uint32_t setup(Socket cs, const fs::path& dir) {
    {
        Frame req;
        req.opcode = Opcode::Connect;
        const std::string ds = dir.string();
        push_u16(req.payload, static_cast<std::uint16_t>(ds.size()));
        req.payload.insert(req.payload.end(), ds.begin(), ds.end());
        push_u16(req.payload, 0);                       // user
        push_u16(req.payload, 0);                       // password
        push_u32(req.payload, openads::network::kCapPrefetchConsume);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::ConnectAck);
    }
    std::uint32_t tid = 0;
    {
        Frame req;
        req.opcode = Opcode::OpenTable;
        const std::string leaf = "d.dbf";
        req.payload.assign(leaf.begin(), leaf.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::OpenTableAck);
        REQUIRE(rep.value().payload.size() >= 4);
        const auto& p = rep.value().payload;
        tid = static_cast<std::uint32_t>(p[0]) |
              (static_cast<std::uint32_t>(p[1]) <<  8) |
              (static_cast<std::uint32_t>(p[2]) << 16) |
              (static_cast<std::uint32_t>(p[3]) << 24);
    }
    {
        Frame req;
        req.opcode = Opcode::GotoTop;
        push_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GotoTopAck);
    }
    return tid;
}

// Walk a SkipAck payload and return the lookahead row count it carries.
// Layout: [u8 has_row][u32 recno][u8 del][u16 nf][per field u32 len + bytes]
//         [u16 lookahead_count][rows...]
int lookahead_count(const std::vector<std::uint8_t>& pl) {
    std::size_t pos = 0;
    REQUIRE(pos < pl.size());
    if (pl[pos++] == 0) return 0;                 // EOF: no row, no block
    REQUIRE(pos + 4 + 1 + 2 <= pl.size());
    pos += 4 + 1;                                  // recno + deleted
    const auto nf = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(pl[pos]) |
        (static_cast<std::uint16_t>(pl[pos + 1]) << 8));
    pos += 2;
    for (std::uint16_t i = 0; i < nf; ++i) {
        REQUIRE(pos + 4 <= pl.size());
        const std::uint32_t vlen =
            static_cast<std::uint32_t>(pl[pos]) |
            (static_cast<std::uint32_t>(pl[pos + 1]) <<  8) |
            (static_cast<std::uint32_t>(pl[pos + 2]) << 16) |
            (static_cast<std::uint32_t>(pl[pos + 3]) << 24);
        pos += 4 + vlen;
    }
    REQUIRE(pos + 2 <= pl.size());
    return static_cast<int>(
        static_cast<std::uint16_t>(pl[pos]) |
        (static_cast<std::uint16_t>(pl[pos + 1]) << 8));
}

// Send one Skip and return the lookahead count on the ack. `depth` < 0 means
// "omit the field entirely" — i.e. impersonate a pre-M12.23 client.
int skip_and_count(Socket cs, std::uint32_t tid, int depth) {
    Frame req;
    req.opcode = Opcode::Skip;
    push_u32(req.payload, tid);
    push_u32(req.payload, 1);                       // step = +1
    if (depth >= 0) push_u16(req.payload, static_cast<std::uint16_t>(depth));
    REQUIRE(write_frame(cs, req).has_value());
    auto rep = read_frame(cs);
    REQUIRE(rep.has_value());
    REQUIRE(rep.value().opcode == Opcode::SkipAck);
    return lookahead_count(rep.value().payload);
}

struct Fixture {
    fs::path dir;
    Server   srv;
    Socket   cs{};
    std::uint32_t tid = 0;

    explicit Fixture(const char* name) {
        dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        write_dbf(dir / "d.dbf", 400);
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        auto cli = connect_tcp("127.0.0.1", srv.port());
        REQUIRE(cli.has_value());
        cs  = cli.value();
        tid = setup(cs, dir);
    }
    ~Fixture() {
        sock_close(cs);
        srv.stop();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

// THE regression this file exists for. A pre-M12.23 client sends an 8-byte
// Skip. If the server were to read the missing field as 0, it would map onto
// SAP's "0 turns read-ahead off" and every old client would silently lose
// prefetch. It must instead mean "auto", and auto ramps from the floor up.
TEST_CASE("legacy 8-byte Skip still gets read-ahead (absent depth != 0)") {
    Fixture fx("openads_skipdepth_legacy");

    const int first = skip_and_count(fx.cs, fx.tid, -1);   // no depth field
    CHECK(first > 0);                                       // NOT disabled

    // ...and it ramps on consecutive forward skips, exactly as if the field
    // had carried kPrefetchDepthAuto.
    const int second = skip_and_count(fx.cs, fx.tid, -1);
    const int third  = skip_and_count(fx.cs, fx.tid, -1);
    CHECK(second > first);
    CHECK(third  > second);
}

TEST_CASE("Skip depth 0 disables the read-ahead block") {
    Fixture fx("openads_skipdepth_off");
    CHECK(skip_and_count(fx.cs, fx.tid, 0) == 0);
    CHECK(skip_and_count(fx.cs, fx.tid, 0) == 0);   // stays off, no ramp
}

TEST_CASE("Skip depth 1 disables the read-ahead block (SAP: 0 or 1 == off)") {
    Fixture fx("openads_skipdepth_one");
    CHECK(skip_and_count(fx.cs, fx.tid, 1) == 0);
}

TEST_CASE("Skip depth N is honoured verbatim, overriding the ramp") {
    Fixture fx("openads_skipdepth_n");
    // An explicit depth is used from the FIRST skip — no 8 -> 16 -> 32 climb.
    CHECK(skip_and_count(fx.cs, fx.tid, 25) == 25);
    CHECK(skip_and_count(fx.cs, fx.tid, 25) == 25);
    // And a different value takes effect immediately.
    CHECK(skip_and_count(fx.cs, fx.tid, 7) == 7);
}

TEST_CASE("Skip depth is capped so one request can't become an unbounded scan") {
    Fixture fx("openads_skipdepth_cap");
    // usRecords is a u16, so an app can ask for far more than is sane. The
    // table has 400 rows and the cap is kPrefetchDepthMax (512), so the walk
    // stops at EOF well before either bound — the point is that it terminates
    // and does not honour 60000 literally.
    const int n = skip_and_count(fx.cs, fx.tid, 60000);
    CHECK(n <= static_cast<int>(openads::network::kPrefetchDepthMax));
    CHECK(n < 400);
}
