#include "doctest.h"
#include "platform/path.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using openads::platform::resolve_case_insensitive;
using openads::platform::resolve_under_root;
using openads::platform::resolve_under_any_root;
using openads::platform::split_data_roots;

TEST_CASE("Case-insensitive resolve returns existing path verbatim") {
    const auto dir = fs::temp_directory_path() / "openads_path_t1";
    fs::create_directories(dir);
    const auto file = dir / "Clientes.dbf";
    { std::ofstream(file) << "x"; }

    auto resolved = resolve_case_insensitive((dir / "Clientes.dbf").string());
    CHECK(resolved == file.string());

    fs::remove_all(dir);
}

TEST_CASE("Case-insensitive resolve matches by case-folded leaf") {
    const auto dir = fs::temp_directory_path() / "openads_path_t2";
    fs::create_directories(dir);
    const auto file = dir / "Clientes.DBF";
    { std::ofstream(file) << "x"; }

    auto resolved = resolve_case_insensitive((dir / "clientes.dbf").string());
    CHECK(resolved == file.string());

    fs::remove_all(dir);
}

TEST_CASE("Case-insensitive resolve returns input on miss") {
    const auto dir = fs::temp_directory_path() / "openads_path_t3";
    fs::create_directories(dir);
    const auto missing = (dir / "Nope.dbf").string();

    auto resolved = resolve_case_insensitive(missing);
    CHECK(resolved == missing);

    fs::remove_all(dir);
}

TEST_CASE("resolve_under_root keeps relative paths inside jail") {
    const auto root = fs::temp_directory_path() / "openads_path_jail";
    const auto sub  = root / "data";
    fs::create_directories(sub);

    auto resolved = resolve_under_root(root.string(), "data");
    REQUIRE(resolved.has_value());
    CHECK(fs::path(*resolved).filename() == "data");

    fs::remove_all(root);
}

TEST_CASE("resolve_under_root rejects parent traversal") {
    const auto root = fs::temp_directory_path() / "openads_path_escape";
    fs::create_directories(root);

    auto resolved = resolve_under_root(root.string(), "..");
    CHECK_FALSE(resolved.has_value());

    fs::remove_all(root);
}

TEST_CASE("split_data_roots splits and trims a ';'-separated list") {
    auto roots = split_data_roots("C:\\a ; C:\\b;C:\\c ");
    REQUIRE(roots.size() == 3);
    CHECK(roots[0] == "C:\\a");
    CHECK(roots[1] == "C:\\b");
    CHECK(roots[2] == "C:\\c");
}

TEST_CASE("split_data_roots drops empty segments and handles a single root") {
    CHECK(split_data_roots("").empty());
    CHECK(split_data_roots("C:\\only").size() == 1);
    CHECK(split_data_roots("C:\\a;;C:\\b").size() == 2);
}

TEST_CASE("resolve_under_any_root accepts a path under any listed root") {
    const auto rootA = fs::temp_directory_path() / "openads_multi_root_a";
    const auto rootB = fs::temp_directory_path() / "openads_multi_root_b";
    fs::create_directories(rootA / "sub");
    fs::create_directories(rootB / "sub");

    const std::vector<std::string> roots{rootA.string(), rootB.string()};

    auto inA = resolve_under_any_root(roots, (rootA / "sub").string());
    REQUIRE(inA.has_value());
    CHECK(fs::path(*inA).filename() == "sub");

    auto inB = resolve_under_any_root(roots, (rootB / "sub").string());
    REQUIRE(inB.has_value());
    CHECK(fs::path(*inB).filename() == "sub");

    const auto outside = fs::temp_directory_path() / "openads_multi_root_outside";
    fs::create_directories(outside);
    auto rejected = resolve_under_any_root(roots, outside.string());
    CHECK_FALSE(rejected.has_value());

    fs::remove_all(rootA);
    fs::remove_all(rootB);
    fs::remove_all(outside);
}
