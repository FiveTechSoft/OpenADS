#include "doctest.h"
#include "platform/fs_sandbox.h"

#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("fs_sandbox: relative path stays under root") {
    auto root = fs::temp_directory_path() / "oads_fs_jail_ok";
    fs::create_directories(root);
    auto r = openads::platform::resolve_fs_path(root.string(), "sub/a.txt");
    REQUIRE(r.has_value());
    auto canon_root = fs::weakly_canonical(root).generic_string();
    CHECK(r->find(canon_root) == 0);
}

TEST_CASE("fs_sandbox: parent escape denied") {
    auto root = fs::temp_directory_path() / "oads_fs_jail_esc";
    fs::create_directories(root);
    auto r = openads::platform::resolve_fs_path(root.string(), "../outside.txt");
    CHECK_FALSE(r.has_value());
}

TEST_CASE("fs_sandbox: wildcard match") {
    CHECK(openads::platform::match_wildcard("foo.dbf", "*.dbf"));
    CHECK_FALSE(openads::platform::match_wildcard("foo.cdx", "*.dbf"));
    CHECK(openads::platform::match_wildcard("ab", "a?"));
    CHECK(openads::platform::match_wildcard("hello", "*"));
}

TEST_CASE("fs_sandbox: fold absolute path") {
    auto rel = openads::platform::fold_absolute_to_relative("C:/app/data/t.txt");
    CHECK(rel.find("C:") == std::string::npos);
    CHECK(rel.find("t.txt") != std::string::npos);
}
