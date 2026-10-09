#include "doctest.h"
#include "test_dd_make.h"
#include "openads/ace.h"
#include "engine/data_dict.h"
#include "engine/sql_input_limits.h"
#include "engine/sql_work_limits.h"
#include "network/client.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using openads::network::Server;
using openads::network::Socket;
using openads::network::connect_tcp;
using openads::network::sock_close;
using openads::network::Frame;
using openads::network::Opcode;
using openads::network::read_frame;
using openads::network::write_frame;

TEST_CASE("M12.3 server Hello → HelloAck round-trip") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    CHECK(srv.running());
    auto port = srv.port();
    REQUIRE(port != 0);

    auto cli = connect_tcp("127.0.0.1", port);
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Hello;
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::HelloAck);
    std::string ver(reply.value().payload.begin(),
                    reply.value().payload.end());
    // Since 1.8.14 the ack carries the real build version ("openads/X.Y.Z"),
    // so a client can tell WHICH serverd is answering; only the prefix is
    // stable across releases.
    CHECK(ver.rfind("openads/", 0) == 0);
    CHECK(ver.size() > std::string("openads/").size());
    CHECK(ver != "openads/0.3.2");   // the pre-1.8.14 hardcoded string

    sock_close(cs);
    srv.stop();
    CHECK_FALSE(srv.running());
}

TEST_CASE("M12.3 server Connect against a real data dir succeeds") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_3_connect";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Connect;
    std::string ds = dir.string();
    auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
        out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    };
    pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
    req.payload.insert(req.payload.end(), ds.begin(), ds.end());
    pushlen(req.payload, 0);                       // empty user
    pushlen(req.payload, 0);                       // empty password
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::ConnectAck);
    std::string s(reply.value().payload.begin(),
                  reply.value().payload.end());
    // Server caps echo: "connected:<dir>" + trailing [u32 LE] word.
    // The dir is echoed verbatim; the caps word advertises SetFields.
    REQUIRE(s.size() >= 10u + ds.size() + 4u);
    CHECK(s.substr(0, 10u + ds.size()) == std::string("connected:") + ds);
    {
        const auto& pl = reply.value().payload;
        const std::uint8_t* c = pl.data() + pl.size() - 4;
        std::uint32_t caps =
            static_cast<std::uint32_t>(c[0]) |
            (static_cast<std::uint32_t>(c[1]) <<  8) |
            (static_cast<std::uint32_t>(c[2]) << 16) |
            (static_cast<std::uint32_t>(c[3]) << 24);
        CHECK((caps & openads::network::kCapSetFieldsBatch) != 0u);
    }

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.3 Connect rejects paths outside server data_dir jail") {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "openads_m12_jail_root";
    auto outside = fs::temp_directory_path() / "openads_m12_jail_out";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(outside, ec);
    fs::create_directories(root);
    fs::create_directories(outside);

    Server srv;
    srv.set_data_dir(root.string());
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    req.opcode = Opcode::Connect;
    std::string ds = "../" + outside.filename().string();
    auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
        out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    };
    pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
    req.payload.insert(req.payload.end(), ds.begin(), ds.end());
    pushlen(req.payload, 0);
    pushlen(req.payload, 0);
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::Error);

    sock_close(cs);
    srv.stop();
    fs::remove_all(root, ec);
    fs::remove_all(outside, ec);
}

TEST_CASE("M12.3 server unknown opcode returns Error frame") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    Frame req;
    // Pick a value outside every defined op so the server's
    // default-case path is what answers. 0xFE was claimed by the
    // Mutex service (M12.32); 0x0F sits in a gap between FindRecordAck
    // (0x0E) and Connect (0x10) with nothing defined.
    req.opcode = static_cast<Opcode>(0x0F);   // truly unknown
    REQUIRE(write_frame(cs, req).has_value());
    auto reply = read_frame(cs);
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::Error);
    // M12.10 — Error payload now starts with [u32 ace_code]; skip 4
    // bytes to recover the textual message.
    REQUIRE(reply.value().payload.size() >= 4);
    std::string s(reply.value().payload.begin() + 4,
                  reply.value().payload.end());
    CHECK(s == "unsupported opcode");

    sock_close(cs);
    srv.stop();
}

namespace {

void m12_write_dbf(const std::filesystem::path& path,
                   const std::vector<std::string>& tags) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    // Record count is a 32-bit LE field at bytes 4..7 — write all four
    // so the fixture stays valid past 255 rows (scale tests).
    {
        auto n = static_cast<std::uint32_t>(tags.size());
        hdr[4] = static_cast<std::uint8_t>( n        & 0xFFu);
        hdr[5] = static_cast<std::uint8_t>((n >>  8) & 0xFFu);
        hdr[6] = static_cast<std::uint8_t>((n >> 16) & 0xFFu);
        hdr[7] = static_cast<std::uint8_t>((n >> 24) & 0xFFu);
    }
    hdr[8]  = 32 + 32 + 1;
    hdr[10] = 1 + 4;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C'; fd[16] = 4;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    for (auto& t : tags) {
        file.push_back(' ');
        for (std::size_t i = 0; i < 4; ++i)
            file.push_back(i < t.size()
                ? static_cast<std::uint8_t>(t[i]) : ' ');
    }
    file.push_back(0x1A);
    std::ofstream(path, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

void m12_write_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>( x        & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >>  8) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 16) & 0xFFu));
    v.push_back(static_cast<std::uint8_t>((x >> 24) & 0xFFu));
}

}  // namespace

TEST_CASE("M12.4 remote OpenTable + GetRecordCount + walk + GetField") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_4";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    // Connect.
    {
        Frame req;
        req.opcode = Opcode::Connect;
        std::string ds = dir.string();
        auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
            out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
        };
        pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
        req.payload.insert(req.payload.end(), ds.begin(), ds.end());
        pushlen(req.payload, 0);                   // user
        pushlen(req.payload, 0);                   // password
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::ConnectAck);
    }

    // OpenTable.
    std::uint32_t tid = 0;
    {
        Frame req;
        req.opcode = Opcode::OpenTable;
        std::string leaf = "data.dbf";
        req.payload.assign(leaf.begin(), leaf.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::OpenTableAck);
        // Warm ack shape: [u32 id][u16 bag_len][bag][u8 section_count]
        // [TLVs...]. The fixture has no production index, so the bag is
        // empty and both warm sections (schema, first row) follow.
        const auto& pl = rep.value().payload;
        REQUIRE(pl.size() > 7);
        tid = static_cast<std::uint32_t>(pl[0]) |
              (static_cast<std::uint32_t>(pl[1]) <<  8) |
              (static_cast<std::uint32_t>(pl[2]) << 16) |
              (static_cast<std::uint32_t>(pl[3]) << 24);
        CHECK(tid == 1);
        CHECK(pl[4] == 0);
        CHECK(pl[5] == 0);   // bag_len == 0 (no index)
        REQUIRE(pl[6] == 2);                // schema + first row
        {
            std::size_t off = 7;
            bool saw_schema = false, saw_row = false;
            for (int s = 0; s < 2; ++s) {
                REQUIRE(off + 5 <= pl.size());
                std::uint8_t tag = pl[off];
                std::uint32_t slen =
                    static_cast<std::uint32_t>(pl[off + 1]) |
                    (static_cast<std::uint32_t>(pl[off + 2]) <<  8) |
                    (static_cast<std::uint32_t>(pl[off + 3]) << 16) |
                    (static_cast<std::uint32_t>(pl[off + 4]) << 24);
                off += 5;
                REQUIRE(off + slen <= pl.size());
                if (tag == 1) {
                    saw_schema = true;
                    CHECK(slen > 2u);
                } else if (tag == 2) {
                    saw_row = true;
                    std::string body(
                        reinterpret_cast<const char*>(&pl[off]), slen);
                    CHECK(body.find("AAAA") != std::string::npos);
                }
                off += slen;
            }
            CHECK(saw_schema);
            CHECK(saw_row);
            CHECK(off == pl.size());
        }
    }

    // GetRecordCount.
    {
        Frame req;
        req.opcode = Opcode::GetRecordCount;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GetRecordCountAck);
        std::uint32_t rc =
            static_cast<std::uint32_t>(rep.value().payload[0]) |
            (static_cast<std::uint32_t>(rep.value().payload[1]) <<  8) |
            (static_cast<std::uint32_t>(rep.value().payload[2]) << 16) |
            (static_cast<std::uint32_t>(rep.value().payload[3]) << 24);
        CHECK(rc == 3);
    }

    // GotoTop + GetField on row 1.
    {
        Frame req;
        req.opcode = Opcode::GotoTop;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GotoTopAck);
    }
    auto get_tag = [&]() {
        Frame req;
        req.opcode = Opcode::GetField;
        m12_write_u32(req.payload, tid);
        std::string fname = "TAG";
        req.payload.insert(req.payload.end(),
                           fname.begin(), fname.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::GetFieldAck);
        return std::string(rep.value().payload.begin(),
                           rep.value().payload.end());
    };
    CHECK(get_tag() == "AAAA");

    // Skip +1 → row 2.
    {
        Frame req;
        req.opcode = Opcode::Skip;
        m12_write_u32(req.payload, tid);
        m12_write_u32(req.payload, 1);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::SkipAck);
    }
    CHECK(get_tag() == "BBBB");

    // CloseTable.
    {
        Frame req;
        req.opcode = Opcode::CloseTable;
        m12_write_u32(req.payload, tid);
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::CloseTableAck);
    }

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.5 dual-mode AdsConnect60 with tcp:// URI routes ABI calls to server") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_5";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());

    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 2);

    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED8  buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "AAAA");

    REQUIRE(AdsSkip(hTable, 1) == 0);
    cap = sizeof(buf); std::memset(buf, 0, sizeof(buf));
    REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
    std::string s2((char*)buf, cap);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "BBBB");

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.14/15 remote field metadata + cursor + info + AOF round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_14_15";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);

    SUBCASE("M12.14 field metadata bridges (no per-field round-trip)") {
        UNSIGNED16 nf = 0;
        REQUIRE(AdsGetNumFields(hT, &nf) == 0);
        CHECK(nf == 1);

        UNSIGNED8  nm[32]  = {0};
        UNSIGNED16 nlen    = sizeof(nm);
        REQUIRE(AdsGetFieldName(hT, 1, nm, &nlen) == 0);
        std::string fname((char*)nm, nlen);
        CHECK(fname == "TAG");

        UNSIGNED16 ftype = 0;
        REQUIRE(AdsGetFieldType(hT, (UNSIGNED8*)"TAG", &ftype) == 0);
        CHECK(ftype == ADS_STRING);

        UNSIGNED32 flen = 0;
        REQUIRE(AdsGetFieldLength(hT, (UNSIGNED8*)"TAG", &flen) == 0);
        CHECK(flen == 4);

        UNSIGNED16 fdec = 99;
        REQUIRE(AdsGetFieldDecimals(hT, (UNSIGNED8*)"TAG", &fdec) == 0);
        CHECK(fdec == 0);
    }

    SUBCASE("M12.14 cursor state bridges") {
        REQUIRE(AdsGotoTop(hT) == 0);

        UNSIGNED16 atbof = 99;
        REQUIRE(AdsAtBOF(hT, &atbof) == 0);
        CHECK(atbof == 0);

        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
        CHECK(rn == 1);

        UNSIGNED16 del = 99;
        REQUIRE(AdsIsRecordDeleted(hT, &del) == 0);
        CHECK(del == 0);

        REQUIRE(AdsGotoBottom(hT) == 0);
        REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
        CHECK(rn == 3);
    }

    SUBCASE("M12.15 info bridges") {
        UNSIGNED16 ttype = 0;
        REQUIRE(AdsGetTableType(hT, &ttype) == 0);
        CHECK(ttype == ADS_CDX);

        UNSIGNED32 rl = 0;
        REQUIRE(AdsGetRecordLength(hT, &rl) == 0);
        CHECK(rl == 5);  // 1 (delete byte) + 4 (TAG)

        UNSIGNED16 ni = 99;
        REQUIRE(AdsGetNumIndexes(hT, &ni) == 0);
        CHECK(ni == 0);
    }

    SUBCASE("M12.15 lock + maintenance bridges (no-op success)") {
        REQUIRE(AdsLockTable(hT) == 0);
        REQUIRE(AdsUnlockTable(hT) == 0);
        REQUIRE(AdsLockRecord(hT, 1) == 0);
        REQUIRE(AdsUnlockRecord(hT, 1) == 0);
        REQUIRE(AdsRefreshRecord(hT) == 0);
        REQUIRE(AdsFlushFileBuffers(hT) == 0);
    }

    SUBCASE("M12.15 AOF over the wire") {
        UNSIGNED8 cond[32] = "TAG = 'BBBB'";
        REQUIRE(AdsSetAOF(hT, cond, 0) == 0);
        UNSIGNED16 lvl = 99;
        REQUIRE(AdsGetAOFOptLevel(hT, &lvl, nullptr, nullptr) == 0);
        CHECK((lvl == ADS_OPTIMIZED_NONE ||
               lvl == ADS_OPTIMIZED_FULL ||
               lvl == ADS_OPTIMIZED_PART));
        REQUIRE(AdsGotoTop(hT) == 0);
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
        CHECK(rn == 2);  // BBBB row only, AAAA / CCCC filtered out
        REQUIRE(AdsClearAOF(hT) == 0);
        REQUIRE(AdsGotoTop(hT) == 0);
        REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
        CHECK(rn == 1);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.6 remote append + set_field + delete + recall + flush round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_6";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());

    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);

    // Append a third record + write its TAG over the wire.
    REQUIRE(AdsAppendRecord(hTable) == 0);
    UNSIGNED8 fld[8] = "TAG";
    UNSIGNED8 val[8] = "CCCC";
    REQUIRE(AdsSetString(hTable, fld, val, 4) == 0);
    REQUIRE(AdsWriteRecord(hTable) == 0);          // wire flush

    // Confirm record_count climbed to 3 + the value round-trips.
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, 0, &cnt) == 0);
    CHECK(cnt == 3);

    REQUIRE(AdsGotoRecord(hTable, 3) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    std::string s((char*)buf, cap);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    CHECK(s == "CCCC");

    // Delete + recall round-trip. Strict GoHot guard: shared-mode writes
    // require an explicit RLock/FLock (Pritpal Bedi contract).
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsLockRecord(hTable, 0) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    UNSIGNED16 del = 0;
    // is_deleted is read-only / local-only here; verify by reopening
    // the file at the end. For now exercise recall.
    REQUIRE(AdsRecallRecord(hTable) == 0);
    REQUIRE(AdsUnlockRecord(hTable, 2) == 0);
    (void)del;

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();

    // Re-open through a *local* connection to confirm the write hit
    // disk on the server side.
    UNSIGNED8 lsrv[256];
    std::memcpy(lsrv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hLocal = 0;
    REQUIRE(AdsConnect60(lsrv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hLocal) == 0);
    ADSHANDLE hLocalT = 0;
    REQUIRE(AdsOpenTable(hLocal, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hLocalT) == 0);
    UNSIGNED32 cnt2 = 0;
    REQUIRE(AdsGetRecordCount(hLocalT, 0, &cnt2) == 0);
    CHECK(cnt2 == 3);
    REQUIRE(AdsGotoRecord(hLocalT, 3) == 0);
    UNSIGNED8 lbuf[16] = {0};
    UNSIGNED32 lcap = sizeof(lbuf);
    REQUIRE(AdsGetField(hLocalT, fld, lbuf, &lcap, 0) == 0);
    std::string s2((char*)lbuf, lcap);
    while (!s2.empty() && s2.back() == ' ') s2.pop_back();
    CHECK(s2 == "CCCC");
    REQUIRE(AdsCloseTable(hLocalT) == 0);
    REQUIRE(AdsDisconnect(hLocal) == 0);

    fs::remove_all(dir, ec);
}

TEST_CASE("M12.7 remote SQL exec — SELECT cursor + COUNT round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_7";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    // 1) SELECT * FROM data — cursor with 3 rows.
    {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql),
                    "SELECT * FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 3);
        REQUIRE(AdsGotoTop(hCur) == 0);
        UNSIGNED8  buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"TAG",
                            buf, &cap, 0) == 0);
        std::string s((char*)buf, cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        CHECK(s == "AAAA");
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    // 2) SELECT COUNT(*) — single-row aggregate cursor.
    {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql),
                    "SELECT COUNT(*) FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.8 remote AdsReindex routes through wire") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_8";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hTable) == 0);
    // No bound indexes — Reindex over an indexless table is a no-op
    // success. The point of the test is that the wire op and the
    // server-side dispatch hold together end-to-end.
    REQUIRE(AdsReindex(hTable) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.9 server with credentials rejects bad password") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_9_bad";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA"});

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 wrong[16] = "nope";
    ADSHANDLE hConn = 0;
    CHECK(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                       user, wrong, 0, &hConn) != 0);
    CHECK(hConn == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.9 server with credentials accepts matching password") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_9_ok";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA"});

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 pw  [16] = "letmein";
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         user, pw, 0, &hConn) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.11 batch fetch returns multiple rows in one frame") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_11";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf",
                  {"AAAA","BBBB","CCCC","DDDD","EEEE","FFFF","GGGG"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string())
                .has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;
    REQUIRE(rc.goto_top(tid).has_value());

    // Fetch up to 5 rows in a single round-trip.
    auto rows_r = rc.fetch_batch(tid, 5, {"TAG"});
    REQUIRE(rows_r.has_value());
    auto rows = rows_r.value();
    REQUIRE(rows.size() == 5);
    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    CHECK(trim(rows[0][0]) == "AAAA");
    CHECK(trim(rows[1][0]) == "BBBB");
    CHECK(trim(rows[2][0]) == "CCCC");
    CHECK(trim(rows[3][0]) == "DDDD");
    CHECK(trim(rows[4][0]) == "EEEE");

    // Continue the walk — should pick up rows 6 + 7, then EOF.
    auto rest_r = rc.fetch_batch(tid, 100, {"TAG"});
    REQUIRE(rest_r.has_value());
    auto rest = rest_r.value();
    REQUIRE(rest.size() == 2);
    CHECK(trim(rest[0][0]) == "FFFF");
    CHECK(trim(rest[1][0]) == "GGGG");

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

// =====================================================================
// Tier-2 — server-side filtered scan (FetchWhere, 0xA4 / 0xA5).
// =====================================================================

TEST_CASE("Tier-2 FetchWhere filters rows server-side in one round-trip") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf",
                  {"AAAA","BBBB","CCCC","DDDD","EEEE","FFFF","GGGG"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;
    REQUIRE(rc.goto_top(tid).has_value());

    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };

    // A predicate the AOF subset can't index-optimise on a plain DBF:
    // the whole 7-row table is scanned server-side and only the 4
    // matching rows cross the wire — in a SINGLE round-trip.
    auto rows_r = rc.fetch_where(tid, 100, "TAG > 'CCCC'", {"TAG"});
    REQUIRE(rows_r.has_value());
    auto rows = rows_r.value().rows;
    REQUIRE(rows.size() == 4);
    CHECK(trim(rows[0][0]) == "DDDD");
    CHECK(trim(rows[1][0]) == "EEEE");
    CHECK(trim(rows[2][0]) == "FFFF");
    CHECK(trim(rows[3][0]) == "GGGG");

    // A predicate matching nothing returns an empty set (still one RTT).
    REQUIRE(rc.goto_top(tid).has_value());
    auto none_r = rc.fetch_where(tid, 100, "TAG = 'ZZZZ'", {"TAG"});
    REQUIRE(none_r.has_value());
    CHECK(none_r.value().rows.empty());

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Tier-2 FetchWhere caps each batch at max_rows matches and resumes") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere_batch";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf",
                  {"AAAA","BBBB","CCCC","DDDD","EEEE","FFFF","GGGG"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;
    REQUIRE(rc.goto_top(tid).has_value());

    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };

    // 5 rows match `TAG >= 'CCCC'` (CCCC..GGGG). With max_rows=2 the scan
    // returns them across 3 batches (2, 2, 1) — the final short batch
    // signals EOF — while every non-matching row is skipped server-side.
    auto b1 = rc.fetch_where(tid, 2, "TAG >= 'CCCC'", {"TAG"});
    REQUIRE(b1.has_value());
    REQUIRE(b1.value().rows.size() == 2);
    CHECK(trim(b1.value().rows[0][0]) == "CCCC");
    CHECK(trim(b1.value().rows[1][0]) == "DDDD");

    auto b2 = rc.fetch_where(tid, 2, "TAG >= 'CCCC'", {"TAG"});
    REQUIRE(b2.has_value());
    REQUIRE(b2.value().rows.size() == 2);
    CHECK(trim(b2.value().rows[0][0]) == "EEEE");
    CHECK(trim(b2.value().rows[1][0]) == "FFFF");

    auto b3 = rc.fetch_where(tid, 2, "TAG >= 'CCCC'", {"TAG"});
    REQUIRE(b3.has_value());
    REQUIRE(b3.value().rows.size() == 1);     // short batch ⇒ EOF
    CHECK(trim(b3.value().rows[0][0]) == "GGGG");

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Tier-2 FetchWhere honours a boolean (.OR.) predicate") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere_bool";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf",
                  {"AAAA","BBBB","CCCC","DDDD","EEEE","FFFF","GGGG"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;
    REQUIRE(rc.goto_top(tid).has_value());

    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };

    auto rows_r = rc.fetch_where(
        tid, 100, "TAG = 'AAAA' .OR. TAG = 'GGGG'", {"TAG"});
    REQUIRE(rows_r.has_value());
    auto rows = rows_r.value().rows;
    REQUIRE(rows.size() == 2);
    CHECK(trim(rows[0][0]) == "AAAA");
    CHECK(trim(rows[1][0]) == "GGGG");

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Tier-2 FetchWhere reply carries the trailing EOF flag + rejects bad id") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere_raw";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA","BBBB"});

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    // Connect.
    {
        Frame req;
        req.opcode = Opcode::Connect;
        std::string ds = dir.string();
        auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
            out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
        };
        pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
        req.payload.insert(req.payload.end(), ds.begin(), ds.end());
        pushlen(req.payload, 0);
        pushlen(req.payload, 0);
        REQUIRE(write_frame(cs, req).has_value());
        REQUIRE(read_frame(cs).has_value());
    }

    // OpenTable.
    std::uint32_t tid = 0;
    {
        Frame req;
        req.opcode = Opcode::OpenTable;
        std::string leaf = "data.dbf";
        req.payload.assign(leaf.begin(), leaf.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::OpenTableAck);
        tid = static_cast<std::uint32_t>(rep.value().payload[0]) |
              (static_cast<std::uint32_t>(rep.value().payload[1]) <<  8) |
              (static_cast<std::uint32_t>(rep.value().payload[2]) << 16) |
              (static_cast<std::uint32_t>(rep.value().payload[3]) << 24);
    }

    auto build_fw = [&](std::uint32_t id, std::uint32_t maxr,
                        const std::string& expr,
                        const std::vector<std::string>& cols) {
        Frame req;
        req.opcode = Opcode::FetchWhere;
        m12_write_u32(req.payload, id);
        m12_write_u32(req.payload, maxr);
        req.payload.push_back(0x00u);   // flags = 0 (backward compat)
        req.payload.push_back(
            static_cast<std::uint8_t>( expr.size()       & 0xFFu));
        req.payload.push_back(
            static_cast<std::uint8_t>((expr.size() >> 8) & 0xFFu));
        req.payload.insert(req.payload.end(), expr.begin(), expr.end());
        req.payload.push_back(static_cast<std::uint8_t>(cols.size()));
        for (auto& c : cols) {
            req.payload.push_back(static_cast<std::uint8_t>(c.size()));
            req.payload.insert(req.payload.end(), c.begin(), c.end());
        }
        return req;
    };

    // Match-all over 2 rows: reply ends with eof == 1.
    {
        REQUIRE(write_frame(cs, build_fw(tid, 100, "TAG > 'AAA'", {"TAG"}))
                    .has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        REQUIRE(rep.value().opcode == Opcode::FetchWhereAck);
        const auto& pl = rep.value().payload;
        REQUIRE(pl.size() >= 6);
        std::uint32_t nrows = static_cast<std::uint32_t>(pl[0]) |
            (static_cast<std::uint32_t>(pl[1]) <<  8) |
            (static_cast<std::uint32_t>(pl[2]) << 16) |
            (static_cast<std::uint32_t>(pl[3]) << 24);
        CHECK(nrows == 2);
        CHECK(pl.back() == 1);                 // scan reached EOF
    }

    // Bad table id ⇒ Error frame, not a malformed ack.
    {
        REQUIRE(write_frame(cs, build_fw(0xDEADBEEF, 10, "TAG > 'A'", {"TAG"}))
                    .has_value());
        auto rep = read_frame(cs);
        REQUIRE(rep.has_value());
        CHECK(rep.value().opcode == Opcode::Error);
    }

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Tier-2 FetchWhere filters a large table server-side at scale") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere_scale";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // 2000 rows, of which exactly 500 carry TAG = "KEEP" (every 4th),
    // interleaved with non-matching "SKIP" rows. A navigational client
    // would read all 2000 over the wire to find the 500; FetchWhere
    // walks them server-side and returns only the matches.
    std::vector<std::string> tags;
    tags.reserve(2000);
    const int kTotal = 2000;
    const int kEvery = 4;            // 1 in 4 matches ⇒ 500 matches
    int expected_matches = 0;
    for (int i = 0; i < kTotal; ++i) {
        if (i % kEvery == 0) { tags.emplace_back("KEEP"); ++expected_matches; }
        else                   tags.emplace_back("SKIP");
    }
    REQUIRE(expected_matches == 500);
    m12_write_dbf(dir / "data.dbf", tags);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;

    auto trim = [](std::string s) {
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };

    // One call, large cap: the whole 2000-row table is scanned
    // server-side and exactly the 500 matches come back.
    REQUIRE(rc.goto_top(tid).has_value());
    auto all_r = rc.fetch_where(tid, 100000, "TAG = 'KEEP'", {"TAG"});
    REQUIRE(all_r.has_value());
    REQUIRE(all_r.value().rows.size() == 500);
    for (auto& row : all_r.value().rows) CHECK(trim(row[0]) == "KEEP");

    // Batched: 250 per call ⇒ exactly 2 batches (250 + 250), the second
    // short of nothing here so a 3rd call returns 0 at EOF. Every
    // batch is bounded by matches, not by rows scanned.
    REQUIRE(rc.goto_top(tid).has_value());
    std::size_t total = 0;
    int calls = 0;
    for (;;) {
        auto b = rc.fetch_where(tid, 250, "TAG = 'KEEP'", {"TAG"});
        REQUIRE(b.has_value());
        ++calls;
        total += b.value().rows.size();
        if (b.value().rows.size() < 250) break;  // short batch ⇒ EOF
        REQUIRE(calls < 10);                       // guard against runaway loop
    }
    CHECK(total == 500);
    CHECK(calls == 3);                        // 250 + 250 + 0(EOF)

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Tier-2 FetchWhere returns recno per row when WANT_RECNO is set") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_t2_fetchwhere_recno";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA","BBBB","CCCC"}); // recnos 1,2,3

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    openads::network::RemoteConnection rc;
    REQUIRE(rc.connect("127.0.0.1", srv.port(), dir.string()).has_value());
    auto tid_r = rc.open_table("data.dbf");
    REQUIRE(tid_r.has_value());
    std::uint32_t tid = tid_r.value().id;
    REQUIRE(rc.goto_top(tid).has_value());

    // TAG >= 'BBBB' matches BBBB (recno 2) and CCCC (recno 3)
    auto r = rc.fetch_where(tid, 10, "TAG >= 'BBBB'", {"TAG"},
                            openads::network::FetchWhereFlags::WANT_RECNO);
    REQUIRE(r.has_value());
    CHECK(r.value().rows.size()     == 2u);   // BBBB, CCCC
    REQUIRE(r.value().recnos.size() == 2u);
    CHECK(r.value().recnos[0]       == 2u);   // BBBB is recno 2
    CHECK(r.value().recnos[1]       == 3u);   // CCCC is recno 3
    CHECK(r.value().eof             == true);

    REQUIRE(rc.close_table(tid).has_value());
    rc.disconnect();
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.13 PlainTransport round-trips frames identically to raw Socket") {
    using openads::network::PlainTransport;
    using openads::network::make_plain_transport;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    auto t = make_plain_transport(cli.value());

    Frame req;
    req.opcode = Opcode::Hello;
    REQUIRE(write_frame(*t, req).has_value());
    auto rep = read_frame(*t);
    REQUIRE(rep.has_value());
    CHECK(rep.value().opcode == Opcode::HelloAck);

    t->close();
    srv.stop();
}

TEST_CASE("M12.12 tls:// URI is parsed + handled per build mode") {
    // tls:// must be recognised symmetrically to tcp://; AdsConnect60
    // routes to either the real TLS path (when built with
    // -DOPENADS_WITH_TLS=ON) or returns AE_FUNCTION_NOT_AVAILABLE
    // 5004 otherwise. Either way it must NEVER silently downgrade
    // to plaintext.
    UNSIGNED8 srvbuf[64];
    // Use port 1 — we only want to confirm the URI was recognised
    // and routed; the TLS handshake will fail (refused) but that's
    // a *remote-error* path, not "function not available".
    std::strcpy(reinterpret_cast<char*>(srvbuf),
                "tls://127.0.0.1:1/whatever");
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                                  nullptr, nullptr, 0, &hConn);
#if defined(OPENADS_WITH_TLS)
    // Real TLS path attempted — connect fails on the local refusal
    // and the server returns an AE_REMOTE_ERROR (5172) frame, but
    // it must NOT be 5004 (function-not-available).
    CHECK(rc != 0u);
    CHECK(rc != 5004u);
#else
    CHECK(rc == 5004u);                            // AE_FUNCTION_NOT_AVAILABLE
#endif
    CHECK(hConn == 0);

    // tls:// parser correctness — split host / port / data_dir.
    std::string h, dd;
    std::uint16_t p = 0;
    REQUIRE(openads::network::parse_tls_uri(
        "tls://server.example:7777/some/dir", h, p, dd));
    CHECK(h == "server.example");
    CHECK(p == 7777u);
    CHECK(dd == "some/dir");

    // tls:// is not parsed by parse_tcp_uri (and vice-versa).
    CHECK_FALSE(openads::network::parse_tcp_uri(
        "tls://x:1/y", h, p, dd));
    CHECK_FALSE(openads::network::parse_tls_uri(
        "tcp://x:1/y", h, p, dd));
}

TEST_CASE("M12.10 server Error frame surfaces the real ACE code") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_m12_10";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    Server srv;
    srv.add_credential("admin", "letmein");
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[256];
    std::snprintf(uri, sizeof(uri),
                  "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 user[16] = "admin";
    UNSIGNED8 wrong[16] = "nope";
    ADSHANDLE hConn = 0;
    UNSIGNED32 rc = AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                                  user, wrong, 0, &hConn);
    // Was AE_INTERNAL_ERROR (5000) before M12.10; now AE_LOGIN_FAILED.
    CHECK(rc == 7077u);
    CHECK(hConn == 0);

    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("M12.3 server stop() drops in-flight connection cleanly") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();
    srv.stop();
    sock_close(cs);
    CHECK_FALSE(srv.running());
}

TEST_CASE("Enterprise: server_max_sessions caps concurrent sessions") {
    Server srv;
    srv.set_max_sessions(2);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto port = srv.port();

    auto hello = [&](Socket cs) {
        Frame req; req.opcode = Opcode::Hello;
        REQUIRE(write_frame(cs, req).has_value());
        auto rep = read_frame(cs);
        return rep.has_value() && rep.value().opcode == Opcode::HelloAck;
    };

    // Two sessions, each held open (their threads block on the next read),
    // so the server sits at capacity.
    auto c1 = connect_tcp("127.0.0.1", port); REQUIRE(c1.has_value());
    CHECK(hello(c1.value()));
    auto c2 = connect_tcp("127.0.0.1", port); REQUIRE(c2.has_value());
    CHECK(hello(c2.value()));

    // A third connection must be refused (server closes it before it serves).
    auto c3 = connect_tcp("127.0.0.1", port); REQUIRE(c3.has_value());
    bool rejected = false;
    for (int i = 0; i < 300 && !rejected; ++i) {
        if (srv.rejected_sessions() >= 1) { rejected = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(rejected);
    CHECK(srv.active_session_threads() <= 2);

    sock_close(c1.value());
    sock_close(c2.value());
    sock_close(c3.value());
    srv.stop();
}

TEST_CASE("Enterprise: finished session threads are reaped (no unbounded growth)") {
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto port = srv.port();

    // Connect / Hello / disconnect many times. Without reaping the server's
    // thread set would grow to ~N; with reaping it stays tiny because each
    // accept first joins+drops the threads whose loop already returned.
    const int N = 30;
    for (int i = 0; i < N; ++i) {
        auto c = connect_tcp("127.0.0.1", port);
        REQUIRE(c.has_value());
        Frame req; req.opcode = Opcode::Hello;
        REQUIRE(write_frame(c.value(), req).has_value());
        auto rep = read_frame(c.value());
        CHECK(rep.has_value());
        sock_close(c.value());
    }

    // Drive a few more accept iterations so the tail gets reaped, and assert
    // the live set stabilises at a small number — not N.
    std::uint32_t active = static_cast<std::uint32_t>(N);
    for (int i = 0; i < 300; ++i) {
        auto c = connect_tcp("127.0.0.1", port);   // each accept reaps first
        if (c.has_value()) sock_close(c.value());
        active = srv.active_session_threads();
        if (active <= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(active <= 3);

    srv.stop();
}

TEST_CASE("Server::build_mg_snapshot counts live sessions") {
    using openads::network::Server;
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    Server::SessionInfo a;
    a.peer_ip = "127.0.0.1"; a.peer_port = 5001;
    a.user = "alice"; a.open_tables = 2;
    std::uint64_t id = srv.register_session(a);

    auto snap = srv.build_mg_snapshot();
    CHECK(snap.connections == 1);
    CHECK(snap.tables == 2);
    REQUIRE(snap.user_list.size() == 1);
    CHECK(snap.user_list[0].name == "alice");

    srv.unregister_session(id);
    srv.stop();
}

namespace {
void srv_set_pool_env(const char* v) {
#ifdef _WIN32
    _putenv_s("OPENADS_SERVER_POOL", v);
#else
    if (v[0] == '\0') ::unsetenv("OPENADS_SERVER_POOL");
    else               ::setenv("OPENADS_SERVER_POOL", v, 1);
#endif
}
std::uint32_t srv_rd_u32(const std::vector<std::uint8_t>& p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
}  // namespace

TEST_CASE("Enterprise pool: OPENADS_SERVER_POOL=1 serves the full wire dispatch") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_pool_dispatch";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    srv_set_pool_env("1");
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv_set_pool_env("");   // clear so only THIS server runs pooled

    auto cli = connect_tcp("127.0.0.1", srv.port());
    REQUIRE(cli.has_value());
    Socket cs = cli.value();

    auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
        out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    };

    // Hello -> HelloAck through the pooled path.
    {
        Frame req; req.opcode = Opcode::Hello;
        REQUIRE(write_frame(cs, req).has_value());
        auto r = read_frame(cs);
        REQUIRE(r.has_value());
        CHECK(r.value().opcode == Opcode::HelloAck);
    }
    // Connect -> ConnectAck (per-session Connection state lives in the pool).
    {
        Frame req; req.opcode = Opcode::Connect;
        std::string ds = dir.string();
        pushlen(req.payload, static_cast<std::uint16_t>(ds.size()));
        req.payload.insert(req.payload.end(), ds.begin(), ds.end());
        pushlen(req.payload, 0);
        pushlen(req.payload, 0);
        REQUIRE(write_frame(cs, req).has_value());
        auto r = read_frame(cs);
        REQUIRE(r.has_value());
        CHECK(r.value().opcode == Opcode::ConnectAck);
    }
    // OpenTable + GetRecordCount — exercises per-session table state on a worker.
    std::uint32_t tid = 0;
    {
        Frame req; req.opcode = Opcode::OpenTable;
        std::string leaf = "data.dbf";
        req.payload.assign(leaf.begin(), leaf.end());
        REQUIRE(write_frame(cs, req).has_value());
        auto r = read_frame(cs);
        REQUIRE(r.has_value());
        REQUIRE(r.value().opcode == Opcode::OpenTableAck);
        tid = srv_rd_u32(r.value().payload);
    }
    {
        Frame req; req.opcode = Opcode::GetRecordCount;
        req.payload.push_back(static_cast<std::uint8_t>( tid        & 0xFFu));
        req.payload.push_back(static_cast<std::uint8_t>((tid >>  8) & 0xFFu));
        req.payload.push_back(static_cast<std::uint8_t>((tid >> 16) & 0xFFu));
        req.payload.push_back(static_cast<std::uint8_t>((tid >> 24) & 0xFFu));
        REQUIRE(write_frame(cs, req).has_value());
        auto r = read_frame(cs);
        REQUIRE(r.has_value());
        REQUIRE(r.value().opcode == Opcode::GetRecordCountAck);
        CHECK(srv_rd_u32(r.value().payload) == 3u);
    }

    // No per-connection session thread was spawned: the pool served it.
    CHECK(srv.active_session_threads() == 0u);

    sock_close(cs);
    srv.stop();
    fs::remove_all(dir, ec);
}

// ADS-unique: server-side SQL exec returns a CURSOR backed by a per-session
// parallel ABI connection (AdsConnect60 + AdsCreateSQLStatement). Under the
// reactor that abi_conn/abi_stmt is created and used on ONE worker thread, so
// this proves the pool preserves the SQL statement/cursor affinity.
TEST_CASE("Enterprise pool: server-side SQL SELECT cursor over the pooled path") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_pool_sql";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    srv_set_pool_env("1");
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv_set_pool_env("");

    char uri[256];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);

    SUBCASE("SELECT * returns a 3-row cursor and the first row reads back") {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql), "SELECT * FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 3);
        REQUIRE(AdsGotoTop(hCur) == 0);
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 cap = sizeof(buf);
        REQUIRE(AdsGetField(hCur, (UNSIGNED8*)"TAG", buf, &cap, 0) == 0);
        std::string s((char*)buf, cap);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        CHECK(s == "AAAA");
        REQUIRE(AdsCloseTable(hCur) == 0);
    }
    SUBCASE("SELECT COUNT(*) is a single-row aggregate cursor") {
        UNSIGNED8 sql[64];
        std::strcpy(reinterpret_cast<char*>(sql),
                    "SELECT COUNT(*) FROM data.dbf");
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        REQUIRE(hCur != 0);
        UNSIGNED32 cnt = 0;
        REQUIRE(AdsGetRecordCount(hCur, 0, &cnt) == 0);
        CHECK(cnt == 1);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    CHECK(srv.active_session_threads() == 0u);   // served by the pool
    srv.stop();
    fs::remove_all(dir, ec);
}

// ADS-unique: AOF (Advantage Optimized Filter) is a server-side filter held in
// the session's engine table state. Through the pool that state must stay on
// the connection's worker and not leak across connections.
TEST_CASE("Enterprise pool: AOF server-side filter over the pooled path") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_pool_aof";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});

    srv_set_pool_env("1");
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv_set_pool_env("");

    char uri[256];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), dir.string().c_str());
    UNSIGNED8 srvbuf[256];
    std::memcpy(srvbuf, uri, std::strlen(uri) + 1);
    UNSIGNED8 leaf[64] = "data.dbf";

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srvbuf, ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hT) == 0);

    UNSIGNED8 cond[32] = "TAG = 'BBBB'";
    REQUIRE(AdsSetAOF(hT, cond, 0) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 2);                       // only the BBBB row passes the filter
    REQUIRE(AdsClearAOF(hT) == 0);
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 1);                       // filter cleared → full table again

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

// ADS-unique: the legacy ERP connects through a DATA DICTIONARY (.add) — tables
// resolved by DD name, not raw file path. This proves a DD connection +
// DD-resolved SQL works over the POOLED wire (the per-session abi_conn inherits
// the DD path on its worker).
//
// SCOPE/HONESTY: the .add here is OpenADS's OWN dictionary format (text header
// "# OpenADS Data Dictionary v1", filled via CREATE TABLE through OpenADS's
// engine) — a round-trip of our own format. It does NOT prove OpenADS can read
// the legacy's REAL Advantage/SAP .add (proprietary DD format); that is a
// separate, unverified engine-track question for the migration.
TEST_CASE("Enterprise pool: Data Dictionary connection + DD-resolved SQL over the pooled wire") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_pool_dd";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    auto add_path = (dir / "app.add").string();

    auto sql_exec = [](ADSHANDLE c, const char* s) -> UNSIGNED32 {
        ADSHANDLE st = 0, cur = 0;
        if (AdsCreateSQLStatement(c, &st) != 0) return 9999;
        std::vector<std::uint8_t> b(std::strlen(s) + 1);
        std::memcpy(b.data(), s, b.size());
        UNSIGNED32 rc = AdsExecuteSQLDirect(st, b.data(), &cur);
        if (cur != 0) AdsCloseTable(cur);
        AdsCloseSQLStatement(st);
        return rc;
    };
    auto sql_count = [](ADSHANDLE c, const char* s) -> UNSIGNED32 {
        ADSHANDLE st = 0, cur = 0;
        if (AdsCreateSQLStatement(c, &st) != 0) return 0xFFFFFFFFu;
        std::vector<std::uint8_t> b(std::strlen(s) + 1);
        std::memcpy(b.data(), s, b.size());
        if (AdsExecuteSQLDirect(st, b.data(), &cur) != 0 || cur == 0) {
            AdsCloseSQLStatement(st);
            return 0xFFFFFFFFu;
        }
        UNSIGNED32 n = 0;
        AdsGetRecordCount(cur, ADS_IGNOREFILTERS, &n);
        AdsCloseTable(cur);
        AdsCloseSQLStatement(st);
        return n;
    };

    // 1. Build a DD with a table + 2 rows via a LOCAL connection.
    //    Seed a real OpenADS-format DD via DataDict::create — a hand-written
    //    "# OpenADS Data Dictionary v1" text stub is not a valid DD signature
    //    (DataDict::load_ rejects it as "unrecognised signature"), which made
    //    the LOCAL AdsConnect60 below fail. Mirrors abi_dd_persistence_test.
    REQUIRE(openads::engine::DataDict::create(add_path).has_value());
    UNSIGNED8 lpath[256];
    std::memcpy(lpath, add_path.c_str(), add_path.size() + 1);
    ADSHANDLE hLocal = 0;
    REQUIRE(AdsConnect60(lpath, ADS_LOCAL_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hLocal) == 0);
    REQUIRE(sql_exec(hLocal,
        "CREATE TABLE clients (ID INTEGER, NAME CHAR(20))") == 0);
    REQUIRE(sql_exec(hLocal,
        "INSERT INTO clients (ID, NAME) VALUES (1, 'Alice')") == 0);
    REQUIRE(sql_exec(hLocal,
        "INSERT INTO clients (ID, NAME) VALUES (2, 'Bob')") == 0);
    REQUIRE(AdsDisconnect(hLocal) == 0);

    // 2. Open the SAME DD through the POOLED wire and query by DD name.
    srv_set_pool_env("1");
    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    srv_set_pool_env("");

    char uri[300];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()), add_path.c_str());
    UNSIGNED8 rpath[300];
    std::memcpy(rpath, uri, std::strlen(uri) + 1);
    ADSHANDLE hRemote = 0;
    REQUIRE(AdsConnect60(rpath, ADS_REMOTE_SERVER, nullptr, nullptr,
                         ADS_DEFAULT, &hRemote) == 0);

    // DD-resolved SQL over the pooled wire: the table is named, not a path.
    CHECK(sql_count(hRemote, "SELECT * FROM clients") == 2u);

    REQUIRE(AdsDisconnect(hRemote) == 0);
    CHECK(srv.active_session_threads() == 0u);   // served by the pool
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("Network login throttling survives reconnect and isolates IP/account counters") {
    Server server;
    CHECK(server.login_allowed("192.0.2.1", "alice"));
    server.login_failed("192.0.2.1", "alice", 1);
    CHECK_FALSE(server.login_allowed("192.0.2.1", "alice"));
    CHECK_FALSE(server.login_allowed("192.0.2.2", "alice"));
    CHECK_FALSE(server.login_allowed("192.0.2.1", "bob"));
    CHECK(server.login_allowed("192.0.2.2", "bob"));
    server.login_succeeded("192.0.2.1", "alice");
    CHECK_FALSE(server.login_allowed("192.0.2.1", "alice"));
    CHECK(server.login_allowed("192.0.2.2", "alice"));
}

TEST_CASE("Network requires Connect before mutex or DD operations") {
    Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    for (auto opcode : {Opcode::Mutex, Opcode::DDCreateUser, Opcode::ExecuteSQL, Opcode::MgRequest}) {
        auto connection = connect_tcp("127.0.0.1", server.port());
        REQUIRE(connection);
        Socket socket = connection.value();
        Frame req;
        req.opcode = opcode;
        REQUIRE(write_frame(socket, req));
        auto response = read_frame(socket);
        REQUIRE(response);
        CHECK(response.value().opcode == Opcode::Error);
        sock_close(socket);
    }
    server.stop();
}

TEST_CASE("Network management session cannot switch to database operations") {
    Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    auto connection = connect_tcp("127.0.0.1", server.port());
    REQUIRE(connection);
    Socket socket = connection.value();
    Frame request;
    request.opcode = Opcode::MgConnect;
    REQUIRE(write_frame(socket, request));
    auto response = read_frame(socket);
    REQUIRE(response);
    REQUIRE(response.value().opcode == Opcode::MgConnectAck);
    request.opcode = Opcode::Connect;
    REQUIRE(write_frame(socket, request));
    response = read_frame(socket);
    REQUIRE(response);
    CHECK(response.value().opcode == Opcode::Error);
    sock_close(socket);
    server.stop();
}

TEST_CASE("Network failed Connect does not authorize a coalesced management mutator") {
    Server server;
    server.add_credential("admin", "secret");
    REQUIRE(server.start("127.0.0.1", 0));
    auto connection = connect_tcp("127.0.0.1", server.port());
    REQUIRE(connection);
    Socket socket = connection.value();
    Frame request;
    request.opcode = Opcode::Connect;
    request.payload = {0, 0, 0, 0, 0, 0};
    auto first = openads::network::encode_frame(request);
    REQUIRE(first);
    request.opcode = Opcode::MgRequest;
    request.payload.clear();
    auto second = openads::network::encode_frame(request);
    REQUIRE(second);
    auto bytes = first.value();
    bytes.insert(bytes.end(), second.value().begin(), second.value().end());
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto result = openads::network::sock_send(socket, bytes.data() + sent, bytes.size() - sent);
        REQUIRE(result);
        REQUIRE(result.value() > 0);
        sent += result.value();
    }
    auto response = read_frame(socket);
    REQUIRE(response);
    CHECK(response.value().opcode == Opcode::Error);
    response = read_frame(socket);
    REQUIRE(response);
    CHECK(response.value().opcode == Opcode::Error);
    sock_close(socket);
    server.stop();
}

TEST_CASE("Network aggregate rejects invalid function and excessive specs") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_aggregate_limits";
    fs::create_directories(dir);
    Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    auto connection = connect_tcp("127.0.0.1", server.port());
    REQUIRE(connection);
    Socket socket = connection.value();
    Frame request;
    request.opcode = Opcode::Connect;
    const auto path = dir.string();
    request.payload = {static_cast<std::uint8_t>(path.size()), static_cast<std::uint8_t>(path.size() >> 8)};
    request.payload.insert(request.payload.end(), path.begin(), path.end());
    request.payload.insert(request.payload.end(), {0, 0, 0, 0});
    REQUIRE(write_frame(socket, request));
    auto response = read_frame(socket);
    REQUIRE(response);
    REQUIRE(response.value().opcode == Opcode::ConnectAck);
    request.opcode = Opcode::Aggregate;
    for (const auto& payload : {
            std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 1, 255, 0},
            std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 33}}) {
        request.payload = payload;
        REQUIRE(write_frame(socket, request));
        response = read_frame(socket);
        REQUIRE(response);
        CHECK(response.value().opcode == Opcode::Error);
        REQUIRE(response.value().payload.size() >= 4);
        const std::string message(response.value().payload.begin() + 4, response.value().payload.end());
        CHECK(message.find("Aggregate:") != std::string::npos);
        CHECK(message.find("bad table id") == std::string::npos);
    }
    sock_close(socket);
    server.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Remote SQL input resource shape ignores literals and comments") {
    using openads::engine::validate_remote_sql_input;
    CHECK(validate_remote_sql_input("SELECT '" + std::string(1000, '(') + "'"));
    CHECK(validate_remote_sql_input("SELECT [" + std::string(1000, '(') + "]"));
    CHECK(validate_remote_sql_input("SELECT 1 /*" + std::string(1000, '(') + "*/"));
    CHECK(validate_remote_sql_input("SELECT 1 --" + std::string(1000, '(')));
    CHECK_FALSE(validate_remote_sql_input("SELECT " + std::string(129, '(') + "1" + std::string(129, ')')));
    CHECK_FALSE(validate_remote_sql_input(std::string(1024u * 1024u + 1, 'x')));
    CHECK_FALSE(validate_remote_sql_input(std::string("SELECT 1\0DROP TABLE users", 25)));
    std::string long_query = "SELECT 1";
    for (int i = 0; i < 3000; ++i) long_query += " + 1";
    CHECK_FALSE(validate_remote_sql_input(long_query));
}

TEST_CASE("Network ExecuteSQL rejects embedded NUL and excessive nesting") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_sql_input_limits";
    fs::create_directories(dir);
    Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    auto connection = connect_tcp("127.0.0.1", server.port());
    REQUIRE(connection);
    Socket socket = connection.value();
    Frame request;
    request.opcode = Opcode::Connect;
    const auto path = dir.string();
    request.payload = {static_cast<std::uint8_t>(path.size()), static_cast<std::uint8_t>(path.size() >> 8)};
    request.payload.insert(request.payload.end(), path.begin(), path.end());
    request.payload.insert(request.payload.end(), {0, 0, 0, 0});
    REQUIRE(write_frame(socket, request));
    auto response = read_frame(socket);
    REQUIRE(response);
    REQUIRE(response.value().opcode == Opcode::ConnectAck);
    request.opcode = Opcode::ExecuteSQL;
    for (const auto& sql : {
            std::string("SELECT 1\0DROP TABLE users", 25),
            "SELECT " + std::string(129, '(') + "1" + std::string(129, ')')}) {
        request.payload.assign(sql.begin(), sql.end());
        REQUIRE(write_frame(socket, request));
        response = read_frame(socket);
        REQUIRE(response);
        CHECK(response.value().opcode == Opcode::Error);
        REQUIRE(response.value().payload.size() >= 4);
        const std::string message(response.value().payload.begin() + 4, response.value().payload.end());
        CHECK(message.find("SQL ") == 0);
    }
    sock_close(socket);
    server.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

#if defined(OPENADS_WITH_TLS)
#include "network/tls_transport.h"
#include "network/worker_pool.h"
namespace {
const char* tls_test_cert = R"PEM(-----BEGIN CERTIFICATE-----
MIIDHzCCAgegAwIBAgIUMiv8kFxegndjtTrZZmvvL5pJEPcwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MTAwMjIzMzY1NVoXDTM2MDky
OTIzMzY1NVowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEApFfhYhCylxtqm19DwpUMaKp/oLTevYnuhHyPf0Gj7hVj
K5m1fhrxqyME46RXuRBFVx4+No+/aNduMigtmQYwILpJRZ/NttgLQbLdj9nEcdJp
xcW2B3B20R6YqZk7jtp1RpETUI5twAjft0wuKD91DuKO8z9WE48gvlZA5F9YiNHV
8GzhE5ezyPh1cuCwglo6Kp/hHdQ3xRXHHKqz8w1hA+VautV2QAXR2dy6GWwdXpRF
XjV13A+N83U6fYU0DHtcB2Ol97/68uS7AuMwPKOo45B8BwkwAZJIeOPVvTX/y28U
L6Im8E0f35kqUz9xfviZVkhf6RcuWenTotIQF+q3xQIDAQABo2kwZzAdBgNVHQ4E
FgQUa8UyGpxY06hNJWGVU/Nn16vLOPQwHwYDVR0jBBgwFoAUa8UyGpxY06hNJWGV
U/Nn16vLOPQwDwYDVR0TAQH/BAUwAwEB/zAUBgNVHREEDTALgglsb2NhbGhvc3Qw
DQYJKoZIhvcNAQELBQADggEBACmmZB7xqro8ePCeQMvJSfilSKQ4JS9Vd38GFxpF
dOB993hx77ATLvtx+HFouetFStIar6y3a38N+tfKle4RqVOP8rDRWEoFFVomqKpj
18Hws0I0+2gZlsBLTV2CUh+jkAqD6td2+C1/NtniJPPLJt5zCJggIAc7CZGiQG2u
AdzFSrK13x04ytWclz9AUemnJrto+q+4QB5RV1LqysoTkmYo+DC8ptvqFDuc231F
qbuGK5kwhigm9qcKQNFVKwr9r6bcnPBapAX5U9cciPvdgDVYbu88peP1TjzRvoIv
qpyFwJ7RqiiCY/wD8ueg73Onw9vNAsjrTOvL6K9xo+Hrb8E=
-----END CERTIFICATE-----
)PEM";
// Test-only key, never deployed. Its only purpose is exercising the handshake.
const char* tls_test_key = R"PEM(-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCkV+FiELKXG2qb
X0PClQxoqn+gtN69ie6EfI9/QaPuFWMrmbV+GvGrIwTjpFe5EEVXHj42j79o124y
KC2ZBjAguklFn8222AtBst2P2cRx0mnFxbYHcHbRHpipmTuO2nVGkRNQjm3ACN+3
TC4oP3UO4o7zP1YTjyC+VkDkX1iI0dXwbOETl7PI+HVy4LCCWjoqn+Ed1DfFFccc
qrPzDWED5Vq61XZABdHZ3LoZbB1elEVeNXXcD43zdTp9hTQMe1wHY6X3v/ry5LsC
4zA8o6jjkHwHCTABkkh449W9Nf/LbxQvoibwTR/fmSpTP3F++JlWSF/pFy5Z6dOi
0hAX6rfFAgMBAAECggEABYG8V6DfmxicFeC3UInWBJvR8vycihVZHK9fz4/PgmbN
D+Tyo+sbNfTScR5MojLdX/Hve7yNWWH+m0elX39JlY8obWDRb9MFbYokynEtl/LJ
AIuDcfRextsTmns796lyTI8H6qcbHlgtxuKSmV6m+Xy19YilxboCCt6xBfX77Xms
vmxAKxoUByNQSkKPSZHtbAXYFJmk31F40y5Pj1Vbf075BKyOmO2gxj1C19fnpjzm
f9pkGcGntEhGS/2QUaD/bGcFYvWs0f+YJHZWt78/BGNtFsxaYHSReM6vcqrlROAC
mzEgVgk4Ol35djVk7IGyVTX8b5ymVtpwwO9BrACIcQKBgQC/p+7opmh8ALXB28EG
zxq14sXYXZWHpHDNyI1CC+FzM66kEcMe+KKTI2sc2FBy0O+3Deh13wLV+Ur6UoLq
yCpO65ocf6biLb3+A0sezlAwx3mqz280q54rT4Sc4dPNSDM7rbJc6AK2Yem+zqd4
R1Os0dybLGtrDkHP4m8qKuaK0wKBgQDbhId17/QZVLG/h96qnh91mRLpOUhPBkIZ
5MT+iN8gGKtAQsSNqcBOlRlbYH8MOhxPRmeVp4L9OvvJOuEkhelozo/5JprBJLHp
wcC7Y19FU0tWUgM6qJyw0bBUNgoS1LAD93nepbgyDJVTtxZxTUFFPfjduyU9hlHL
ZtrmeoPkBwKBgC72fJFfrXytQ9xz99GuUBI/tlE1ZV2uisGyIgMMHDt5b5Lek1x0
eonphOa1jskDr6nAa7TuZ6h9BMVgEJptYAikrMfM89y6brLepbqvvXCmgIa9e7eB
Uim0u38hyx+jUIKQJoOjin6ccYWC6ACOIc/YQOF5Of0qqi/BgZHon0NnAoGAfeUq
EzeE1So/rsrrpwp8nGMn914E3F2Id3U+jYROAwhi3r3sIBrk0aytGDzlYEvLhKOq
MKgbdcPoN2ZvTRUH4jXlWE0NoAu9hYS7Vj0NnKLCqETs2S1uf/IioIlFibs1H3FF
Ea4VT47r7VEpq91Uu7NpETGNkBWCv5bDoD9PeO0CgYEApaxZ4QfRDtOJu1zZxfVA
v4i9zdrjCKLMHVupUpFtPQH4HIrEOnQlVcCjAdu5SWIsWkT6A0ilOnN/izKf1z/4
kypB5jbwAh6MMMxUQ8VDVx2GXYzNiU8NVw6SIcMRirJ560o9x3q1RqH8zDB2NFnA
jpaoCC0GFWggLtLPIvszZig=
-----END PRIVATE KEY-----
)PEM";
}
TEST_CASE("native TLS listener validates config and serves encrypted Hello") {
    openads::network::TlsConfig server_config;
    server_config.cert_pem = tls_test_cert;
    server_config.key_pem = tls_test_key;
    Server server;
    openads::network::TlsConfig invalid;
    CHECK_FALSE(server.set_tls(invalid).has_value());
    invalid.cert_pem = tls_test_cert;
    invalid.key_pem = "not a PEM private key";
    CHECK_FALSE(server.set_tls(invalid).has_value());
    REQUIRE(server.set_tls(server_config).has_value());
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    openads::network::TlsConfig client_config;
    client_config.ca_pem = tls_test_cert;
    client_config.sni_hostname = "localhost";
    auto client = openads::network::connect_tls("127.0.0.1", server.port(), client_config);
    REQUIRE(client.has_value());
    Frame request;
    request.opcode = Opcode::Hello;
    REQUIRE(write_frame(*client.value(), request).has_value());
    auto response = read_frame(*client.value());
    REQUIRE(response.has_value());
    CHECK(response.value().opcode == Opcode::HelloAck);
    // A wire frame larger than one TLS record is split by the transport.
    request.payload.resize(20000, 42);
    REQUIRE(write_frame(*client.value(), request).has_value());
    REQUIRE(read_frame(*client.value()).has_value());
    client.value()->close();
    client_config.sni_hostname = "wrong-host.invalid";
    CHECK_FALSE(openads::network::connect_tls("127.0.0.1", server.port(), client_config).has_value());
    server.stop();
}
TEST_CASE("native TLS single-worker pool serves a client while another handshake stalls") {
    openads::network::TlsConfig config;
    config.cert_pem = tls_test_cert;
    config.key_pem = tls_test_key;
    Server server;
    REQUIRE(server.set_tls(config).has_value());
    openads::network::WorkerPool pool(server, 1);
    pool.start();
    auto listener = openads::network::listen_tcp({"127.0.0.1", 0, 4});
    REQUIRE(listener.has_value());
    auto port = openads::network::socket_local_port(listener.value());
    REQUIRE(port.has_value());
    auto stalled = connect_tcp("127.0.0.1", port.value());
    REQUIRE(stalled.has_value());
    auto accepted = openads::network::accept_one(listener.value());
    REQUIRE(accepted.has_value());
    pool.submit(accepted.value(), "", port.value());
    // Truncated ClientHello record: no worker may wait for its remainder.
    const std::uint8_t partial[] = {22, 3, 3, 0, 20, 1};
    REQUIRE(openads::network::sock_send(stalled.value(), partial, sizeof(partial)).has_value());
    openads::network::TlsConfig client_config;
    client_config.ca_pem = tls_test_cert;
    client_config.sni_hostname = "localhost";
    std::thread accept_thread([&] {
        auto incoming = openads::network::accept_one(listener.value());
        if (incoming) pool.submit(incoming.value(), "", port.value());
    });
    auto client = openads::network::connect_tls("127.0.0.1", port.value(), client_config);
    accept_thread.join();
    REQUIRE(client.has_value());
    Frame hello;
    hello.opcode = Opcode::Hello;
    REQUIRE(write_frame(*client.value(), hello).has_value());
    auto reply = read_frame(*client.value());
    REQUIRE(reply.has_value());
    CHECK(reply.value().opcode == Opcode::HelloAck);
    client.value()->close();
    sock_close(stalled.value());
    pool.stop();
    sock_close(listener.value());
}
namespace {
std::atomic<long long> tls_idle_offset{0};
std::chrono::steady_clock::time_point tls_idle_now() noexcept {
    return std::chrono::steady_clock::now()+std::chrono::seconds(tls_idle_offset.load());
}
}
TEST_CASE("mtfix41 daemon TLS established idle resumes in dedicated and reactor") {
    namespace fs=std::filesystem;
    auto dir=fs::temp_directory_path()/"openads_tls_idle41";
    std::error_code ec; fs::create_directories(dir,ec);
    for(bool reactor : {false,true}) {
        tls_idle_offset.store(0);
        Server server; server.set_session_clock(tls_idle_now);
        openads::network::TlsConfig sc; sc.cert_pem=tls_test_cert; sc.key_pem=tls_test_key;
        REQUIRE(server.set_tls(sc));
        std::unique_ptr<openads::network::WorkerPool> pool;
        Socket listener; std::uint16_t port=0;
        if(reactor) {
            auto l=openads::network::listen_tcp({"127.0.0.1",0,4}); REQUIRE(l); listener=l.value();
            auto p=openads::network::socket_local_port(listener); REQUIRE(p); port=p.value();
            pool=std::make_unique<openads::network::WorkerPool>(server,1); pool->start();
        } else { REQUIRE(server.start("127.0.0.1",0)); port=server.port(); }
        std::thread accepter;
        if(reactor) accepter=std::thread([&] { auto peer=openads::network::accept_one(listener); if(peer)pool->submit(peer.value(),dir.string(),port); });
        openads::network::TlsConfig cc; cc.ca_pem=tls_test_cert; cc.sni_hostname="localhost";
        auto transport=openads::network::connect_tls("127.0.0.1",port,cc);
        if(accepter.joinable()) accepter.join();
        REQUIRE(transport);
        openads::network::RemoteConnection client;
        REQUIRE(client.connect_with_transport(std::move(transport).value(),dir.string()));
        tls_idle_offset.store(2700); std::this_thread::sleep_for(std::chrono::milliseconds(450));
        // Opening a nonexistent table yields a server Error, not transport loss.
        auto absent=client.open_table("missing.dbf"); CHECK_FALSE(absent); CHECK(client.valid());
        CHECK(absent.error().message!="peer closed connection");
        client.disconnect();
        if(pool)pool->stop(); else server.stop();
        if(listener.valid())sock_close(listener);
    }
    tls_idle_offset.store(0); fs::remove_all(dir,ec);
}
#endif

TEST_CASE("remote SQL shape budget checks overflow and conservative outer-join fanout") {
    using Budget = openads::engine::RemoteSqlShapeBudget;
    Budget boundary;
    CHECK(boundary.add_source(100000, 16).has_value());
    Budget too_many;
    CHECK_FALSE(too_many.add_source(100001, 16).has_value());
    Budget join;
    REQUIRE(join.add_source(1000, 16).has_value());
    CHECK_FALSE(join.add_source(1000, 16).has_value());
    Budget wide;
    CHECK_FALSE(wide.add_source(100000, 1024).has_value());
    Budget empty_outer;
    REQUIRE(empty_outer.add_source(0, 16).has_value());
    CHECK_FALSE(empty_outer.add_source(100001, 16).has_value());
    Budget huge;
    CHECK_FALSE(huge.add_source(UINT64_MAX, UINT64_MAX).has_value());
}

TEST_CASE("remote SQL join preflight rejects fanout before materialization; local unchanged") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_sql_join_budget";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "a.dbf", std::vector<std::string>(400, "SAME"));
    m12_write_dbf(dir / "b.dbf", std::vector<std::string>(400, "SAME"));
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + dir.string();
    ADSHANDLE connection = 0, statement = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& query, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(query.begin(), query.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    ADSHANDLE cursor = 0;
    CHECK(execute("SELECT a.TAG FROM a.dbf a INNER JOIN b.dbf b ON a.TAG = b.TAG", &cursor) != 0);
    CHECK(cursor == 0);
    REQUIRE(execute("SELECT COUNT(*) FROM a.dbf", &cursor) == 0);
    REQUIRE(cursor != 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    // Direct local SQL remains available for larger trusted batch work.
    std::string local = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(local.data()), ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    REQUIRE(execute("SELECT COUNT(*) FROM a.dbf a INNER JOIN b.dbf b ON a.TAG = b.TAG", &cursor) == 0);
    REQUIRE(cursor != 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("remote DML target preflight rejects before any row changes and keeps connection usable") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_sql_dml_budget";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "big.dbf", std::vector<std::string>(100001, "SAME"));
    m12_write_dbf(dir / "small.dbf", {"SAME"});
    auto read_bytes = [](const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto original = read_bytes(dir / "big.dbf");
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + dir.string();
    ADSHANDLE connection = 0, statement = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& query, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(query.begin(), query.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    for (const auto& query : {
            "UPDATE big.dbf SET TAG = 'EDIT'",
            "DELETE FROM big.dbf",
            "MERGE INTO big.dbf ON TAG = 'SAME' WHEN MATCHED THEN UPDATE SET TAG = 'EDIT'"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(query));
        CHECK(execute(query, &cursor) != 0);
        CHECK(cursor == 0);
        UNSIGNED32 code = 0;
        UNSIGNED16 length = 2048;
        UNSIGNED8 message[2048]{};
        REQUIRE(AdsGetLastError(&code, message, &length) == 0);
        // ExecuteSQL intentionally returns generic text over the wire.
        CHECK(code == 7200);
        CHECK(std::string(reinterpret_cast<char*>(message)).find("server-side exec failed") != std::string::npos);
    }
    ADSHANDLE cursor = 0;
    REQUIRE(execute("UPDATE small.dbf SET TAG = 'EDIT'", &cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    CHECK(read_bytes(dir / "big.dbf") == original);
    // Trusted local batch scan remains available, even above the remote bound.
    std::string local = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(local.data()), ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    REQUIRE(execute("UPDATE big.dbf SET TAG = 'EDIT' WHERE TAG = 'NONE'", &cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("remote SQL UNION additive budget checks rows bytes and overflow") {
    using Budget = openads::engine::RemoteSqlUnionBudget;
    Budget rows;
    REQUIRE(rows.add(60000, 16).has_value());
    CHECK_FALSE(rows.add(60000, 16).has_value());
    Budget bytes;
    REQUIRE(bytes.add(40000, 1024).has_value());
    CHECK_FALSE(bytes.add(40000, 1024).has_value());
    Budget exact;
    REQUIRE(exact.add(100000, 16).has_value());
    CHECK(exact.add(0, 16).has_value());
    CHECK_FALSE(exact.add(1, 16).has_value());
    Budget overflow;
    CHECK_FALSE(overflow.add(UINT64_MAX, UINT64_MAX).has_value());
}

TEST_CASE("remote UNION rejects combined staging even when each member fits; local unchanged") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_sql_union_budget";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "a.dbf", std::vector<std::string>(60000, "SAME"));
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + dir.string();
    ADSHANDLE connection = 0, statement = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& query, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(query.begin(), query.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    ADSHANDLE cursor = 0;
    for (const auto& query : {
            "SELECT TAG FROM a.dbf UNION ALL SELECT TAG FROM a.dbf",
            "SELECT TAG FROM a.dbf UNION SELECT TAG FROM a.dbf"}) {
        INFO(std::string(query));
        CHECK(execute(query, &cursor) != 0);
        CHECK(cursor == 0);
        UNSIGNED32 code = 0;
        UNSIGNED16 length = 2048;
        UNSIGNED8 message[2048]{};
        REQUIRE(AdsGetLastError(&code, message, &length) == 0);
        CHECK(code == 7200);
        CHECK(std::string(reinterpret_cast<char*>(message)).find("server-side exec failed") != std::string::npos);
    }
    REQUIRE(execute("SELECT COUNT(*) FROM a.dbf", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    std::string local = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(local.data()), ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    REQUIRE(execute("SELECT TAG FROM a.dbf UNION ALL SELECT TAG FROM a.dbf", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("remote predicate subquery preflight rejects quadratic scans before cursor exposure") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_sql_predicate_budget";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "a.dbf", std::vector<std::string>(1001, "SAME"));
    m12_write_dbf(dir / "b.dbf", std::vector<std::string>(1001, "SAME"));
    m12_write_dbf(dir / "small.dbf", {"SAME"});
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + dir.string();
    ADSHANDLE connection = 0, statement = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& query, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(query.begin(), query.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    for (const auto& query : {
            "SELECT TAG FROM a.dbf WHERE EXISTS (SELECT TAG FROM b.dbf WHERE TAG = 'NONE')",
            "SELECT TAG FROM a.dbf WHERE TAG IN (SELECT TAG FROM b.dbf)",
            "SELECT TAG FROM a.dbf WHERE TAG = (SELECT TAG FROM b.dbf)"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(query));
        CHECK(execute(query, &cursor) == 7200);
        CHECK(cursor == 0);
    }
    ADSHANDLE cursor = 0;
    REQUIRE(execute("SELECT TAG FROM small.dbf WHERE EXISTS (SELECT TAG FROM b.dbf)", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    std::string local = dir.string();
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(local.data()), ADS_LOCAL_SERVER,
                        nullptr, nullptr, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    REQUIRE(execute("SELECT TAG FROM a.dbf WHERE TAG IN (SELECT TAG FROM b.dbf)", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    fs::remove_all(dir, error);
}

TEST_CASE("remote SQL DD named-user rights reject ignored-rights join and inline subquery") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_remote_sql_dd_acl";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "allowed.dbf", {"SAME"});
    m12_write_dbf(dir / "hidden.dbf", {"SECR"});
    openads_test::make_dd(dir / "test.add",
        "TABLE allowed=allowed.dbf\nTABLE hidden=hidden.dbf\n"
        "USER alice\nUSERPROP alice;prop_1101=pw\nDBPROP prop_5=1\n"
        "TABLEPERM allowed;alice=1\nTABLEPERM hidden;alice=0\n");
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + (dir / "test.add").string();
    UNSIGNED8 user[] = "alice", password[] = "pw";
    ADSHANDLE connection = 0, statement = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        user, password, 0, &connection) == 0);
    REQUIRE(AdsCreateSQLStatement(connection, &statement) == 0);
    auto execute = [&](const std::string& text, ADSHANDLE* cursor) {
        std::vector<UNSIGNED8> sql(text.begin(), text.end());
        sql.push_back(0);
        return AdsExecuteSQLDirect(statement, sql.data(), cursor);
    };
    for (const auto& text : {
            "SELECT * FROM hidden",
            "SELECT allowed.TAG FROM allowed INNER JOIN hidden ON allowed.TAG = hidden.TAG",
            "SELECT TAG FROM allowed WHERE EXISTS (SELECT TAG FROM hidden)",
            "UPDATE allowed SET TAG = 'EDIT'",
            "DROP TABLE allowed", "ALTER TABLE allowed ADD COLUMN EXTRA CHAR(4)",
            "CREATE TABLE denied (TAG CHAR(4))", "CREATE INDEX denied ON allowed (TAG)",
            "CREATE DATABASE 'denied.add'"}) {
        ADSHANDLE cursor = 0;
        INFO(std::string(text));
        CHECK(execute(text, &cursor) == 7200);
        CHECK(cursor == 0);
    }
    ADSHANDLE cursor = 0;
    CHECK(fs::exists(dir / "allowed.dbf"));
    CHECK_FALSE(fs::exists(dir / "denied.dbf"));
    CHECK_FALSE(fs::exists(dir / "denied.add"));
    REQUIRE(execute("SELECT TAG FROM allowed", &cursor) == 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    fs::remove_all(dir, error);
}


TEST_CASE("wire DD table rights authorize direct engine reads writes and maintenance") {
    namespace fs = std::filesystem;
    using DD = openads::engine::DataDict;
    const auto dir = fs::temp_directory_path() / "openads_wire_dd_table_acl";
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    m12_write_dbf(dir / "readable.dbf", {"READ"});
    m12_write_dbf(dir / "hidden.dbf", {"HIDE"});
    m12_write_dbf(dir / "columns.dbf", {"COLS"});
    m12_write_dbf(dir / "writable.dbf", {"EDIT"});
    openads_test::make_dd(dir / "test.add",
        "TABLE readable=readable.dbf\nTABLE hidden=hidden.dbf\n"
        "TABLE columns=columns.dbf\nTABLE writable=writable.dbf\n"
        "USER alice\nUSERPROP alice;prop_1101=pw\nDBPROP prop_5=1\n"
        "TABLEPERM readable;alice=1\nTABLEPERM hidden;alice=0\n"
        "TABLEPERM columns;alice=1\nTABLEPERM writable;alice=3\n");
    {
        auto result = DD::open((dir / "test.add").string());
        REQUIRE(result.has_value());
        REQUIRE(result.value().grant_column_permission("columns", "TAG", "alice", DD::DD_PERM_SELECT).has_value());
    }
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + (dir / "test.add").string();
    UNSIGNED8 user[] = "alice", password[] = "pw";
    ADSHANDLE connection = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                        user, password, 0, &connection) == 0);
    auto open = [&](const std::string& name, ADSHANDLE* table) {
        std::vector<UNSIGNED8> bytes(name.begin(), name.end()); bytes.push_back(0);
        return AdsOpenTable(connection, bytes.data(), nullptr, ADS_CDX, 0, 0, 0, ADS_SHARED, table);
    };
    for (const auto& name : {"hidden", "hidden.dbf", "columns", "columns.dbf"}) {
        ADSHANDLE table = 0;
        INFO(std::string(name));
        CHECK(open(name, &table) == 7079);
        CHECK(table == 0);
    }
    ADSHANDLE table = 0;
    REQUIRE(open("readable", &table) == 0);
    REQUIRE(AdsGotoTop(table) == 0);
    UNSIGNED8 field[] = "TAG", value[] = "FAIL";
    CHECK(AdsSetString(table, field, value, 4) == 7079);
    CHECK(AdsAppendRecord(table) == 7079);
    CHECK(AdsDeleteRecord(table) == 7079);
    CHECK(AdsLockRecord(table, 1) == 7079);
    CHECK(AdsZapTable(table) == 7079);
    CHECK(AdsPackTable(table) == 7079);
    CHECK(AdsReindex(table) == 7079);
    REQUIRE(AdsCloseTable(table) == 0);
    REQUIRE(open("writable", &table) == 0);
    REQUIRE(AdsGotoTop(table) == 0);
    REQUIRE(AdsLockRecord(table, 1) == 0);
    UNSIGNED8 edit[] = "PASS";
    CHECK(AdsSetString(table, field, edit, 4) == 0);
    CHECK(AdsFlushFileBuffers(table) == 0);
    REQUIRE(AdsCloseTable(table) == 0);
    UNSIGNED8 name[] = "hidden";
    CHECK(AdsDropTable(connection, name, 1) == 7079);
    CHECK(fs::exists(dir / "hidden.dbf"));
    REQUIRE(AdsDisconnect(connection) == 0);
    server.stop();
    fs::remove_all(dir, error);
}

TEST_CASE("Fetch and FetchWhere refuse values beyond u16 without truncation") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_fetch_wire_limits";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string root = dir.string();
    std::vector<UNSIGNED8> rootbuf(root.begin(), root.end());
    rootbuf.push_back(0);
    ADSHANDLE local = 0, table = 0;
    REQUIRE(AdsConnect60(rootbuf.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &local) == 0);
    UNSIGNED8 name[] = "limits.dbf", definition[] = "TEXT,M,10,0", field[] = "TEXT";
    REQUIRE(AdsCreateTable(local, name, nullptr, ADS_CDX, 0, 0, 0, 0, definition, &table) == 0);
    for (std::size_t size : {65535u, 65536u, 70000u}) {
        std::string value(size, 'X');
        REQUIRE(AdsAppendRecord(table) == 0);
        REQUIRE(AdsSetString(table, field, reinterpret_cast<UNSIGNED8*>(value.data()),
                             static_cast<UNSIGNED32>(value.size())) == 0);
        REQUIRE(AdsWriteRecord(table) == 0);
    }
    REQUIRE(AdsCloseTable(table) == 0);
    REQUIRE(AdsDisconnect(local) == 0);
    openads::network::Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    openads::network::RemoteConnection client;
    REQUIRE(client.connect("127.0.0.1", server.port(), root));
    const auto opened = client.open_table("limits.dbf");
    REQUIRE(opened);
    const auto id = opened.value().id;
    for (int mode = 0; mode < 2; ++mode) {
        REQUIRE(client.goto_record(id, 1));
        if (mode == 0) {
            auto good = client.fetch_batch(id, 1, {"TEXT"});
            REQUIRE(good);
            REQUIRE(good.value().size() == 1);
            CHECK(good.value()[0][0] == std::string(65535, 'X'));
        } else {
            auto good = client.fetch_where(id, 1, "", {"TEXT"});
            REQUIRE(good);
            REQUIRE(good.value().rows.size() == 1);
            CHECK(good.value().rows[0][0] == std::string(65535, 'X'));
        }
        for (std::uint32_t row : {2u, 3u}) {
            REQUIRE(client.goto_record(id, row));
            if (mode == 0) CHECK_FALSE(client.fetch_batch(id, 1, {"TEXT"}));
            else CHECK_FALSE(client.fetch_where(id, 1, "", {"TEXT"}));
        }
    }
    auto cursor = client.execute_sql("SELECT TEXT FROM limits");
    REQUIRE(cursor);
    REQUIRE(client.goto_top(cursor.value()));
    auto cursor_good = client.fetch_batch(cursor.value(), 1, {"TEXT"});
    REQUIRE(cursor_good);
    REQUIRE(cursor_good.value().size() == 1);
    CHECK(cursor_good.value()[0][0] == std::string(65535, 'X'));
    CHECK_FALSE(client.fetch_batch(cursor.value(), 1, {"TEXT"}));
    REQUIRE(client.goto_top(cursor.value()));
    CHECK_FALSE(client.fetch_batch(cursor.value(), 1, {"MISSING"}));
    REQUIRE(client.close_table(cursor.value()));
    REQUIRE(client.goto_record(id, 1));
    CHECK_FALSE(client.fetch_batch(id, 1, {"MISSING"}));
    CHECK_FALSE(client.fetch_where(id, 1, "", {"MISSING"}));
    REQUIRE(client.close_table(id));
    client.disconnect();
    server.stop();
    fs::remove_all(dir, ec);
}

#include "engine/sql_execution_budget.h"

TEST_CASE("SQL execution budget returns failure rather than a partial success cursor") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_sql_shared_steps";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    m12_write_dbf(dir / "data.dbf", {"AAAA", "BBBB", "CCCC"});
    auto root = dir.string();
    std::vector<UNSIGNED8> path(root.begin(), root.end());
    path.push_back(0);
    ADSHANDLE conn = 0, statement = 0;
    REQUIRE(AdsConnect60(path.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &conn) == 0);
    REQUIRE(AdsCreateSQLStatement(conn, &statement) == 0);
    UNSIGNED8 sql[] = "SELECT TAG FROM data WHERE TAG <> 'NOPE' ORDER BY TAG";
    ADSHANDLE cursor = 0;
    {
        openads::engine::SqlExecutionScope small(true, 2, 60000);
        CHECK(AdsExecuteSQLDirect(statement, sql, &cursor) != 0);
        CHECK(cursor == 0);
        CHECK(openads::engine::sql_execution_exhausted());
    }
    REQUIRE(AdsExecuteSQLDirect(statement, sql, &cursor) == 0);
    REQUIRE(cursor != 0);
    REQUIRE(AdsCloseTable(cursor) == 0);
    REQUIRE(AdsCloseSQLStatement(statement) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
    fs::remove_all(dir, ec);
}
