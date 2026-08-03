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

TEST_CASE("fs_sandbox: is_client_absolute is host-independent") {
    using openads::platform::is_client_absolute;
    CHECK(is_client_absolute("C:/app/t.dbf"));
    CHECK(is_client_absolute("e:\\app\\t.dbf"));
    CHECK(is_client_absolute("/srv/data/t.dbf"));
    CHECK(is_client_absolute("\\\\srv\\share\\t.dbf"));
    CHECK_FALSE(is_client_absolute("sub/t.dbf"));
    CHECK_FALSE(is_client_absolute("t.dbf"));
    CHECK_FALSE(is_client_absolute(""));
}

TEST_CASE("fs_sandbox: legacy resolver prefix-strips the data root") {
    namespace plat = openads::platform;
    auto root = fs::temp_directory_path() / "oads_legacy_prefix";
    fs::create_directories(root);
    const std::string root_s = root.generic_string();
    const std::string canon_root =
        fs::weakly_canonical(root).generic_string();

    // Same path as the root, spelled uppercase + backslashes: the root
    // prefix is stripped case-insensitively and the remainder re-joined
    // with its original casing.
    std::string upper = root_s;
    for (auto& c : upper) {
        if (c == '/') c = '\\';
        else if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    auto r = plat::resolve_client_path({root_s}, upper + "\\SubDir\\File.DBF");
    REQUIRE(r.has_value());
    CHECK(*r == canon_root + "/SubDir/File.DBF");

    // A different drive letter is ignored when matching the root prefix.
    const std::string no_drive =
        (root_s.size() >= 2 && root_s[1] == ':') ? root_s.substr(2) : root_s;
    auto r2 = plat::resolve_client_path({root_s}, "E:" + no_drive + "/x.dbf");
    REQUIRE(r2.has_value());
    CHECK(*r2 == canon_root + "/x.dbf");

    // The root path itself maps to the root directory.
    auto r3 = plat::resolve_client_path({root_s}, upper);
    REQUIRE(r3.has_value());
    CHECK(*r3 == canon_root);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("fs_sandbox: legacy resolver folds foreign absolute paths") {
    namespace plat = openads::platform;
    auto root = fs::temp_directory_path() / "oads_legacy_fold";
    fs::create_directories(root);
    const std::string canon_root =
        fs::weakly_canonical(root).generic_string();

    // No root prefix in common: drop the drive and join the remainder,
    // so both client folders of an ERP live under the one --data root.
    auto r = plat::resolve_client_path(
        {root.generic_string()}, "E:\\CREATIVE.RAM\\C0000001\\B5643DS1.dbf");
    REQUIRE(r.has_value());
    CHECK(*r == canon_root + "/CREATIVE.RAM/C0000001/B5643DS1.dbf");

    // A POSIX-spelled root still matches a Windows client path whose
    // remainder starts with it (Windows client -> Linux server case).
    auto r2 = plat::resolve_client_path(
        {"/srv/data"}, "E:\\srv\\data\\Sub\\t.dbf");
    REQUIRE(r2.has_value());
    CHECK(r2->size() >= std::string("/srv/data/Sub/t.dbf").size());
    CHECK(r2->compare(r2->size() - std::string("/srv/data/Sub/t.dbf").size(),
                      std::string::npos, "/srv/data/Sub/t.dbf") == 0);

    // Empty and drive-root-only paths resolve to the first root itself.
    auto r3 = plat::resolve_client_path({root.generic_string()}, "E:/");
    REQUIRE(r3.has_value());
    CHECK(*r3 == canon_root);
    auto r4 = plat::resolve_client_path({root.generic_string()}, "");
    REQUIRE(r4.has_value());
    CHECK(*r4 == canon_root);

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("fs_sandbox: legacy resolver leaves relative paths alone") {
    namespace plat = openads::platform;
    auto root = fs::temp_directory_path() / "temp";
    fs::create_directories(root);
    const std::string canon_root =
        fs::weakly_canonical(root).generic_string();

    // A relative path that merely starts with the root's folder name is
    // NOT prefix-stripped — stripping is for client-absolute paths only.
    auto r = plat::resolve_client_path({root.generic_string()}, "temp/x.dbf");
    REQUIRE(r.has_value());
    CHECK(*r == canon_root + "/temp/x.dbf");

    // Jail escape is still denied.
    auto r2 = plat::resolve_client_path({root.generic_string()},
                                        "E:/../../evil.dbf");
    CHECK_FALSE(r2.has_value());

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST_CASE("fs_sandbox: legacy resolver tries roots in order") {
    namespace plat = openads::platform;
    auto root_a = fs::temp_directory_path() / "oads_legacy_ra";
    auto root_b = fs::temp_directory_path() / "oads_legacy_rb";
    fs::create_directories(root_a);
    fs::create_directories(root_b);
    const std::string canon_b =
        fs::weakly_canonical(root_b).generic_string();

    // The path names root B's prefix: it resolves under B, not A.
    auto r = plat::resolve_client_path(
        {root_a.generic_string(), root_b.generic_string()},
        root_b.generic_string() + "/t.dbf");
    REQUIRE(r.has_value());
    CHECK(*r == canon_b + "/t.dbf");

    std::error_code ec;
    fs::remove_all(root_a, ec);
    fs::remove_all(root_b, ec);
}
