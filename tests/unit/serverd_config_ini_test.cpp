#include "doctest.h"
#include "tools/serverd/config_ini.h"

#include <string>

using openads::serverd::IniConfig;
using openads::serverd::parse_ini;

namespace {

IniConfig parse_ok(const std::string& text) {
    IniConfig cfg;
    std::string err;
    bool ok = parse_ini(text, cfg, err);
    INFO("parse error: ", err);
    REQUIRE(ok);
    return cfg;
}

}  // namespace

TEST_CASE("parse_ini reads every recognised key") {
    auto cfg = parse_ok(
        "host = 127.0.0.1\n"
        "port = 6263\n"
        "backlog = 32\n"
        "max_sessions = 700\n"
        "http_port = 8080\n"
        "data = C:/app/data\n"
        "http_user = admin:secret\n"
        "auth_user = client:secret2\n");
    CHECK(cfg.has_host);
    CHECK(cfg.host == "127.0.0.1");
    CHECK(cfg.has_port);
    CHECK(cfg.port == 6263);
    CHECK(cfg.has_backlog);
    CHECK(cfg.backlog == 32);
    CHECK(cfg.has_max_sessions);
    CHECK(cfg.max_sessions == 700);
    CHECK(cfg.has_http_port);
    CHECK(cfg.http_port == 8080);
    CHECK(cfg.has_data);
    CHECK(cfg.data_dir == "C:/app/data");
    REQUIRE(cfg.http_users.size() == 1);
    CHECK(cfg.http_users[0].first == "admin");
    CHECK(cfg.http_users[0].second == "secret");
    REQUIRE(cfg.auth_users.size() == 1);
    CHECK(cfg.auth_users[0].first == "client");
    CHECK(cfg.auth_users[0].second == "secret2");
}

TEST_CASE("parse_ini leaves the has_* flags clear for absent keys") {
    auto cfg = parse_ok("port = 6262\n");
    CHECK(cfg.has_port);
    CHECK_FALSE(cfg.has_host);
    CHECK_FALSE(cfg.has_backlog);
    CHECK_FALSE(cfg.has_http_port);
    CHECK_FALSE(cfg.has_data);
    CHECK(cfg.http_users.empty());
    CHECK(cfg.auth_users.empty());
}

TEST_CASE("parse_ini ignores comments, blanks and a [server] header") {
    auto cfg = parse_ok(
        "# OpenADS server config\n"
        "; semicolon comment\n"
        "\n"
        "[server]\n"
        "   port   =   6263   \n");
    CHECK(cfg.has_port);
    CHECK(cfg.port == 6263);
}

TEST_CASE("parse_ini tolerates CRLF line endings") {
    auto cfg = parse_ok("host = 0.0.0.0\r\nport = 6262\r\n");
    CHECK(cfg.host == "0.0.0.0");
    CHECK(cfg.port == 6262);
}

TEST_CASE("parse_ini accepts dash/underscore aliases") {
    auto cfg = parse_ok("http-port = 9000\ndata_dir = /var/lib/openads\n");
    CHECK(cfg.http_port == 9000);
    CHECK(cfg.data_dir == "/var/lib/openads");
}

TEST_CASE("parse_ini max_sessions accepts dash alias and zero") {
    auto cfg = parse_ok("max-sessions = 1000\n");
    CHECK(cfg.has_max_sessions);
    CHECK(cfg.max_sessions == 1000);
    auto zero = parse_ok("maxsessions = 0\n");
    CHECK(zero.has_max_sessions);
    CHECK(zero.max_sessions == 0);   // 0 = unlimited (valid)
    IniConfig bad;
    std::string err;
    CHECK_FALSE(parse_ini("max_sessions = -5\n", cfg, err));
    CHECK(err.find("max_sessions") != std::string::npos);
}

TEST_CASE("parse_ini EnableFileFunc") {
    auto cfg = parse_ok("EnableFileFunc = 1\n");
    CHECK(cfg.has_enable_file_func);
    CHECK(cfg.enable_file_func);
    auto off = parse_ok("enable_file_func = false\n");
    CHECK(off.has_enable_file_func);
    CHECK_FALSE(off.enable_file_func);
}

TEST_CASE("parse_ini collects repeated http_user lines") {
    auto cfg = parse_ok("http_user = a:1\nhttp_user = b:2\n");
    REQUIRE(cfg.http_users.size() == 2);
    CHECK(cfg.http_users[0].first == "a");
    CHECK(cfg.http_users[1].first == "b");
}

TEST_CASE("parse_ini rejects an out-of-range port") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("port = 99999\n", cfg, err));
    CHECK(err.find("port") != std::string::npos);
}

TEST_CASE("parse_ini rejects a non-numeric port") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("port = abc\n", cfg, err));
}

TEST_CASE("parse_ini rejects an unknown key with a line number") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("port = 6262\nfoo = bar\n", cfg, err));
    CHECK(err.find("line 2") != std::string::npos);
    CHECK(err.find("foo") != std::string::npos);
}

TEST_CASE("parse_ini rejects http_user without a colon") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("http_user = adminsecret\n", cfg, err));
}

TEST_CASE("parse_ini rejects auth_user without a colon") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("auth_user = clientsecret\n", cfg, err));
}

TEST_CASE("parse_ini rejects a line with no equals sign") {
    IniConfig cfg;
    std::string err;
    CHECK_FALSE(parse_ini("port 6262\n", cfg, err));
}
