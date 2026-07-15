#include "doctest.h"
#include "network/client.h"

#include <cstdint>
#include <string>

using openads::network::parse_tcp_uri;
using openads::network::parse_tls_uri;

TEST_CASE("parse_tcp_uri: basic valid URI") {
    std::string host, dir;
    std::uint16_t port = 0;
    REQUIRE(parse_tcp_uri("tcp://localhost:16262//tmp/data", host, port, dir));
    CHECK(host == "localhost");
    CHECK(port == 16262);
    CHECK(dir == "/tmp/data");
}

TEST_CASE("parse_tcp_uri: IP address") {
    std::string host, dir;
    std::uint16_t port = 0;
    REQUIRE(parse_tcp_uri("tcp://192.168.1.100:2020/C:/data", host, port, dir));
    CHECK(host == "192.168.1.100");
    CHECK(port == 2020);
    CHECK(dir == "C:/data");
}

TEST_CASE("parse_tcp_uri: rejects URI without port") {
    std::string host, dir;
    std::uint16_t port = 0;
    CHECK_FALSE(parse_tcp_uri("tcp://myhost/mydir", host, port, dir));
}

TEST_CASE("parse_tcp_uri: rejects non-tcp scheme") {
    std::string host, dir;
    std::uint16_t port = 0;
    CHECK_FALSE(parse_tcp_uri("udp://host:16262/dir", host, port, dir));
    CHECK_FALSE(parse_tcp_uri("http://host:16262/dir", host, port, dir));
    CHECK_FALSE(parse_tcp_uri("sqlite:///tmp/db", host, port, dir));
}

TEST_CASE("parse_tcp_uri: rejects empty string") {
    std::string host, dir;
    std::uint16_t port = 0;
    CHECK_FALSE(parse_tcp_uri("", host, port, dir));
}

TEST_CASE("parse_tls_uri: basic valid URI") {
    std::string host, dir;
    std::uint16_t port = 0;
    REQUIRE(parse_tls_uri("tls://secure.host:443/secure/data", host, port, dir));
    CHECK(host == "secure.host");
    CHECK(port == 443);
    CHECK(dir == "secure/data");
}

TEST_CASE("parse_tls_uri: rejects non-tls scheme") {
    std::string host, dir;
    std::uint16_t port = 0;
    CHECK_FALSE(parse_tls_uri("tcp://host:443/dir", host, port, dir));
    CHECK_FALSE(parse_tls_uri("http://host:443/dir", host, port, dir));
}

TEST_CASE("parse_tls_uri: rejects empty string") {
    std::string host, dir;
    std::uint16_t port = 0;
    CHECK_FALSE(parse_tls_uri("", host, port, dir));
}

TEST_CASE("RemoteTable: default constructed state") {
    openads::network::RemoteTable rt;
    CHECK(rt.conn == nullptr);
    CHECK(rt.id == 0);
    CHECK(rt.name.empty());
    CHECK_FALSE(rt.fields_cached);
    CHECK_FALSE(rt.row_valid);
    CHECK(rt.current_recno == 0);
    CHECK_FALSE(rt.current_deleted);
    CHECK(rt.cached_rec_count == 0);
    CHECK_FALSE(rt.rec_count_cached);
    CHECK(rt.prefetch_queue.empty());
    CHECK(rt.cursor_lag == 0);
    CHECK(rt.prefetch_dir == 0);
    CHECK_FALSE(rt.found_cached);
    CHECK_FALSE(rt.current_found);
}

TEST_CASE("RemoteIndex: default constructed state") {
    openads::network::RemoteIndex ri;
    CHECK(ri.conn == nullptr);
    CHECK(ri.id == 0);
    CHECK(ri.tbl_id == 0);
    CHECK(ri.parent == nullptr);
}

TEST_CASE("RemoteConnection: default constructed is not valid") {
    openads::network::RemoteConnection rc;
    CHECK_FALSE(rc.valid());
}
