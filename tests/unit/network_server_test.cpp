#include "doctest.h"
#include "openads/ace.h"
#include "engine/data_dict.h"
#include "network/client.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"

#include <array>
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
    CHECK(s == std::string("connected:") + ds);

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
    // default-case path is what answers. The server filesystem API
    // (b06cf6a0) claimed 0xE0-0xFD; 0xFE is the only remaining gap
    // below Error (0xFF).
    req.opcode = static_cast<Opcode>(0xFE);   // truly unknown
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
        REQUIRE(rep.value().payload.size() == 4);
        tid = static_cast<std::uint32_t>(rep.value().payload[0]) |
              (static_cast<std::uint32_t>(rep.value().payload[1]) <<  8) |
              (static_cast<std::uint32_t>(rep.value().payload[2]) << 16) |
              (static_cast<std::uint32_t>(rep.value().payload[3]) << 24);
        CHECK(tid == 1);
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

    // Delete + recall round-trip.
    REQUIRE(AdsGotoRecord(hTable, 2) == 0);
    REQUIRE(AdsDeleteRecord(hTable) == 0);
    UNSIGNED16 del = 0;
    // is_deleted is read-only / local-only here; verify by reopening
    // the file at the end. For now exercise recall.
    REQUIRE(AdsRecallRecord(hTable) == 0);
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
