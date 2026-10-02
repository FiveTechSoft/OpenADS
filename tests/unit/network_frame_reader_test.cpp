#include "doctest.h"

#include "network/frame_reader.h"
#include "network/wire.h"

#include <cstdint>
#include <vector>

using openads::network::FrameReader;
using openads::network::Frame;
using openads::network::Opcode;

namespace {

// Build one wire frame: [len:u32 BE][opcode][payload].
std::vector<std::uint8_t> frame_bytes(Opcode op,
                                      std::vector<std::uint8_t> payload) {
    auto n = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> b = {
        static_cast<std::uint8_t>((n >> 24) & 0xFF),
        static_cast<std::uint8_t>((n >> 16) & 0xFF),
        static_cast<std::uint8_t>((n >> 8) & 0xFF),
        static_cast<std::uint8_t>(n & 0xFF),
        static_cast<std::uint8_t>(op)};
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

} // namespace

TEST_CASE("FrameReader holds a frame split across reads until complete") {
    FrameReader fr;
    auto bytes = frame_bytes(Opcode::Skip, {'h', 'i'});  // 7 bytes total

    auto r1 = fr.feed(bytes.data(), 3);  // partial header only
    REQUIRE(r1);
    CHECK(r1.value().empty());
    CHECK(fr.buffered() == 3);

    auto r2 = fr.feed(bytes.data() + 3, bytes.size() - 3);  // the rest
    REQUIRE(r2);
    REQUIRE(r2.value().size() == 1);
    CHECK(r2.value()[0].opcode == Opcode::Skip);
    CHECK(r2.value()[0].payload == std::vector<std::uint8_t>{'h', 'i'});
    CHECK(fr.buffered() == 0);
}

TEST_CASE("FrameReader decodes several frames from one feed, keeps a partial tail") {
    FrameReader fr;
    auto a = frame_bytes(Opcode::GotoTop, {});       // empty payload
    auto b = frame_bytes(Opcode::Skip, {'x'});
    std::vector<std::uint8_t> buf = a;
    buf.insert(buf.end(), b.begin(), b.end());
    // Append a partial third frame (just its header) — must be retained.
    buf.insert(buf.end(), {0, 0, 0, 4, static_cast<std::uint8_t>(Opcode::Skip)});

    auto r = fr.feed(buf.data(), buf.size());
    REQUIRE(r);
    REQUIRE(r.value().size() == 2);
    CHECK(r.value()[0].opcode == Opcode::GotoTop);
    CHECK(r.value()[0].payload.empty());
    CHECK(r.value()[1].opcode == Opcode::Skip);
    CHECK(r.value()[1].payload == std::vector<std::uint8_t>{'x'});
    CHECK(fr.buffered() == 5);  // the retained partial header
}

TEST_CASE("FrameReader rejects a frame declaring an over-large payload") {
    FrameReader fr;
    // len = 0xFFFFFFFF (~4 GB) > kMaxFramePayload.
    std::vector<std::uint8_t> bad = {0xFF, 0xFF, 0xFF, 0xFF,
                                     static_cast<std::uint8_t>(Opcode::Skip)};
    auto r = fr.feed(bad.data(), bad.size());
    CHECK_FALSE(r);
}

TEST_CASE("FrameReader enforces preauth header limit and permits authenticated upgrade") {
    FrameReader fr;
    fr.set_payload_limit(64 * 1024);
    const std::uint8_t bad[] = {0, 1, 0, 1, 0x10};
    CHECK_FALSE(fr.feed(bad, sizeof(bad)));
    FrameReader other;
    other.set_payload_limit(64 * 1024);
    auto connect = frame_bytes(Opcode::Connect, {});
    auto large = frame_bytes(Opcode::ExecuteSQL, std::vector<std::uint8_t>(65537, 'x'));
    connect.insert(connect.end(), large.begin(), large.end());
    auto first = other.feed(connect.data(), connect.size(), 1);
    REQUIRE(first);
    REQUIRE(first.value().size() == 1);
    CHECK(first.value().front().opcode == Opcode::Connect);
    other.set_payload_limit(openads::network::kMaxFramePayload);
    auto second = other.feed(nullptr, 0, 1);
    REQUIRE(second);
    REQUIRE(second.value().size() == 1);
    CHECK(second.value().front().payload.size() == 65537);
}

TEST_CASE("FrameReader rejects unknown opcode without waiting for its body") {
    FrameReader reader;
    const std::uint8_t header[] = {0, 0, 0, 20, 0};
    CHECK_FALSE(reader.feed(header, sizeof(header)));
    const std::uint8_t complete[] = {0, 0, 0, 0, 0};
    CHECK_FALSE(openads::network::decode_frame(complete, sizeof(complete), nullptr));
}

TEST_CASE("FrameReader accepts OpenIndexAck paired enum declaration") {
    FrameReader reader;
    auto bytes = frame_bytes(Opcode::OpenIndexAck, {0, 0});
    auto result = reader.feed(bytes.data(), bytes.size());
    REQUIRE(result);
    REQUIRE(result.value().size() == 1);
    CHECK(result.value().front().opcode == Opcode::OpenIndexAck);
    CHECK(openads::network::valid_opcode(static_cast<std::uint8_t>(Opcode::OpenIndexAck)));
}
