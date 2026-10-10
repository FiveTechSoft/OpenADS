// tests/unit/network_use_budget_test.cpp
// Per-USE wire budget (Vouch WAN startup). One realistic rddads-style
// USE cycle — open + setorder + gotop×2 + bof/eof pairs + bottom×2 +
// keycount + close — must cost a bounded number of server frames, with
// zero boundary-probe frames (sticky/twin) and zero duplicate navs:
//   OpenTable + CloseTable + GotoTop + GotoBottom + SetOrder +
//   KeyCount×2 = 9 frames (session setup excluded: the Hello/Connect
//   handshake and the session-pool lanes are established once per
//   logical connection and amortised over every USE).
#include "doctest.h"
#include "mgmt/mg_stats.h"
#include "network/server.h"
#include "network/session.h"
#include "network/client.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

std::uint64_t ub_op(std::uint8_t op) {
    return openads::mgmt::process_mg_stats()
        .op_timing[op]
        .count.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("Nav batching: full USE cycle stays within wire budget") {
    auto dir = fs::temp_directory_path() / "openads_usebudget";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hC0 = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hC0)
            == AE_SUCCESS);
    UNSIGNED8 def[] = "ID,N,8,0";
    UNSIGNED8 tn0[] = "UB.DBF";
    ADSHANDLE hT0 = 0;
    REQUIRE(AdsCreateTable(hC0, tn0, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT0)
            == AE_SUCCESS);
    UNSIGNED8 f1[] = "ID";
    for (int i = 1; i <= 50; ++i) {
        REQUIRE(AdsAppendRecord(hT0) == AE_SUCCESS);
        REQUIRE(AdsSetDouble(hT0, f1, i * 10) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT0) == AE_SUCCESS);
    }
    ADSHANDLE hI0 = 0;
    UNSIGNED8 bag[] = "UB.CDX";
    SUBCASE("production CDX") {}
    SUBCASE("production Z01") { std::memcpy(bag, "UB.Z01", sizeof(bag)); }
    UNSIGNED8 tag[] = "BYID";
    UNSIGNED8 exp[] = "ID";
    REQUIRE(AdsCreateIndex61(hT0, bag, tag, exp, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hI0) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT0) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC0) == AE_SUCCESS);

    openads::network::Server s;
    REQUIRE(s.start("127.0.0.1", 0).has_value());

    char uri[512]{};
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(s.port()), dir.string().c_str());
    UNSIGNED8 sb[512]{};
    std::memcpy(sb, uri, std::strlen(uri) + 1);
    ADSHANDLE hC = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hC)
            == AE_SUCCESS);

    const auto open_index_before = ub_op(0x88);
    // Snapshot AFTER connect: the session pool (one Connect per lane,
    // OPENADS_POOL_SIZE) is established once per logical connection and
    // amortised over every USE — the budget below guards the per-USE
    // cycle (open + setorder + gotop x2 + bof/eof + bottom x2 + keycount
    // + close), not session setup.
    std::map<std::uint8_t, std::uint64_t> before;
    for (int o = 0; o < 256; ++o)
        before[static_cast<std::uint8_t>(o)] = ub_op((std::uint8_t)o);
    UNSIGNED8 tn[] = "UB.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hC, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &hT) == AE_SUCCESS);
    UNSIGNED32 record_len = 0; const auto rl_before = ub_op(0x6C);
    REQUIRE(AdsGetRecordLength(hT, &record_len) == AE_SUCCESS);
    CHECK(record_len == 9); CHECK(ub_op(0x6C) == rl_before);
    const auto oi_after_open = ub_op(0x88);
    ADSHANDLE duplicated[64]{}; UNSIGNED16 duplicated_count = 64;
    REQUIRE(AdsOpenIndex(hT, bag, duplicated, &duplicated_count) == AE_SUCCESS);
    CHECK(duplicated_count == 1); CHECK(ub_op(0x88) == oi_after_open);
    UNSIGNED32 natural_rec = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &natural_rec) == AE_SUCCESS);
    CHECK(natural_rec == 1);
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want[] = "BYID";
    REQUIRE(AdsGetIndexHandle(hT, want, &hOrd) == AE_SUCCESS);
    REQUIRE(AdsSetIndexOrderByHandle(hT, hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hOrd) == AE_SUCCESS);
    UNSIGNED16 b = 0, e = 0;
    REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
    REQUIRE(AdsAtBOF(hT, &b) == AE_SUCCESS);
    REQUIRE(AdsAtEOF(hT, &e) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
    REQUIRE(AdsGotoBottom(hOrd) == AE_SUCCESS);
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 50u);
    CHECK(ub_op(0x88) == open_index_before); // production binding rode OpenTable
    auto delta = [&](std::uint8_t op) { return ub_op(op) - before[op]; };
    std::uint64_t total = 0;
    for (int o = 0; o < 256; ++o)
        total += ub_op((std::uint8_t)o) - before[(std::uint8_t)o];

    // The whole cycle, connect included, fits in a small fixed budget —
    // boundary probes and duplicate navs contribute zero frames.
    CHECK(total <= 14u);
    CHECK(delta(0x40) == 1u);  // GotoTop: second suppressed
    CHECK(delta(0x64) == 0u);  // mtfix15 restores opposite-boundary pair;
                               // bottom and duplicate are served locally
    CHECK(delta(0x48) == 0u);  // AtEOF: served locally
    CHECK(delta(0x4C) == 0u);  // AtBOF: served locally

    // A separate login gets independent server index IDs and cursor state.
    ADSHANDLE second_conn = 0, second_table = 0;
    REQUIRE(AdsConnect60(sb, ADS_REMOTE_SERVER, nullptr, nullptr, 0, &second_conn) == AE_SUCCESS);
    REQUIRE(AdsOpenTable(second_conn, tn, nullptr, ADS_CDX, 0, 0, 0, 0, &second_table) == AE_SUCCESS);
    ADSHANDLE second_order = 0; REQUIRE(AdsGetIndexHandle(second_table, want, &second_order) == AE_SUCCESS);
    CHECK(second_order != hOrd);
    REQUIRE(AdsSetIndexOrderByHandle(second_table, second_order) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(second_order) == AE_SUCCESS);
    UNSIGNED32 first_rec = 0, second_rec = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &first_rec) == AE_SUCCESS);
    REQUIRE(AdsGetRecordNum(second_table, 0, &second_rec) == AE_SUCCESS);
    CHECK(first_rec == 50); CHECK(second_rec == 1);
    // CloseAll / same-bag reopen keeps its existing validated park path.
    REQUIRE(AdsCloseAllIndexes(second_table) == AE_SUCCESS);
    duplicated_count = 64;
    REQUIRE(AdsOpenIndex(second_table, bag, duplicated, &duplicated_count) == AE_SUCCESS);
    CHECK(duplicated_count == 1);
    REQUIRE(AdsCloseTable(second_table) == AE_SUCCESS); REQUIRE(AdsDisconnect(second_conn) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hC) == AE_SUCCESS);

    s.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Open metadata: legacy clients do not receive setup sections") {
    // Reuse the table created by the USE fixture only after creating our own
    // independent small DBF, so this test never depends on test order.
    auto dir = fs::temp_directory_path() / "openads_metadata_cap";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
    UNSIGNED8 local[512]{}; std::memcpy(local, dir.string().c_str(), dir.string().size());
    ADSHANDLE c = 0, t = 0;
    REQUIRE(AdsConnect60(local, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &c) == AE_SUCCESS);
    UNSIGNED8 name[] = "cap.dbf", def[] = "ID,N,8,0";
    REQUIRE(AdsCreateTable(c, name, nullptr, ADS_CDX, 0, 0, 0, 0, def, &t) == AE_SUCCESS);
    ADSHANDLE index = 0; UNSIGNED8 bag[] = "cap.cdx", tag[] = "BYID", expression[] = "ID";
    REQUIRE(AdsCreateIndex61(t, bag, tag, expression, nullptr, nullptr,
                             ADS_COMPOUND, 512, &index) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(t) == AE_SUCCESS); REQUIRE(AdsDisconnect(c) == AE_SUCCESS);
    openads::network::Server server;
    for (bool supports_metadata : {false, true}) {
        openads::network::Session session(server, openads::network::Socket{}, "", 0);
        openads::network::Frame connect; connect.opcode = openads::network::Opcode::Connect;
        auto text = [&](const std::string& value) {
            connect.payload.push_back(static_cast<std::uint8_t>(value.size() & 255));
            connect.payload.push_back(static_cast<std::uint8_t>(value.size() >> 8));
            connect.payload.insert(connect.payload.end(), value.begin(), value.end());
        };
        text(dir.string()); text(""); text("");
        const std::uint32_t caps = supports_metadata ? openads::network::kCapOpenSetupMetadata : 0;
        for (int i = 0; i < 4; ++i) connect.payload.push_back(static_cast<std::uint8_t>(caps >> (8 * i)));
        auto login = session.dispatch(connect); REQUIRE(login.reply);
        REQUIRE(login.reply->opcode == openads::network::Opcode::ConnectAck);
        openads::network::Frame open; open.opcode = openads::network::Opcode::OpenTable;
        open.payload.assign(name, name + std::strlen(reinterpret_cast<char*>(name)));
        auto result = session.dispatch(open); REQUIRE(result.reply);
        REQUIRE(result.reply->opcode == openads::network::Opcode::OpenTableAck);
        const auto& pl = result.reply->payload; REQUIRE(pl.size() >= 7);
        const auto baglen = static_cast<std::size_t>(pl[4] | (pl[5] << 8));
        std::size_t off = 6 + baglen; REQUIRE(off < pl.size());
        const unsigned count = pl[off++]; bool found_length = false, found_index = false;
        for (unsigned i = 0; i < count; ++i) {
            REQUIRE(off + 5 <= pl.size()); const auto tlv_tag = pl[off++];
            const std::size_t length = static_cast<std::uint32_t>(pl[off]) |
                (static_cast<std::uint32_t>(pl[off+1]) << 8) |
                (static_cast<std::uint32_t>(pl[off+2]) << 16) |
                (static_cast<std::uint32_t>(pl[off+3]) << 24);
            off += 4; REQUIRE(off + length <= pl.size());
            if (tlv_tag == openads::network::OpenTableAckSections::kRecordLength) {
                found_length = true; CHECK(length == 4); CHECK(pl[off] == 9);
            }
            if (tlv_tag == openads::network::OpenTableAckSections::kProductionIndex) {
                found_index = true;
                std::vector<std::uint8_t> bytes(pl.begin() + off, pl.begin() + off + length);
                auto parsed = openads::network::RemoteConnection::parse_open_index_reply(bytes, "cap.cdx");
                REQUIRE(parsed); REQUIRE(parsed.value().size() == 1);
                CHECK(parsed.value()[0].tag == "BYID");
            }
            off += length;
        }
        CHECK(found_index == supports_metadata);
        CHECK(found_length == supports_metadata);
        CHECK(off == pl.size());
    }
    fs::remove_all(dir, ec);
}

TEST_CASE("Open metadata: shared index parser rejects short mandatory payloads") {
    using C = openads::network::RemoteConnection;
    CHECK_FALSE(C::parse_open_index_reply({}, "bag.cdx"));
    CHECK_FALSE(C::parse_open_index_reply({1}, "bag.cdx"));
    CHECK_FALSE(C::parse_open_index_reply({1, 0, 1, 0, 0}, "bag.cdx"));
    CHECK_FALSE(C::parse_open_index_reply({1, 0, 1, 0, 0, 0, 3, 0, 'x'}, "bag.cdx"));
    auto legacy = C::parse_open_index_reply({1, 0, 42, 0, 0, 0}, "bag.cdx");
    REQUIRE(legacy); REQUIRE(legacy.value().size() == 1);
    CHECK(legacy.value()[0].id == 42);
    CHECK(legacy.value()[0].tag.empty());
}
