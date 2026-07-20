#include "doctest.h"
#include "engine/server_fs.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("server_fs: write size rename erase") {
    auto dir = fs::temp_directory_path() / "oads_server_fs_ut";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto p = (dir / "t.txt").string();
    {
        auto f = openads::engine::fs_open(
            p, openads::engine::ADS_FO_READWRITE, true);
        REQUIRE(f);
        f.value()->stream.write("hi", 2);
        f.value()->stream.flush();
    }
    auto sz = openads::engine::fs_size(p);
    REQUIRE(sz);
    CHECK(sz.value() == 2);
    auto p2 = (dir / "u.txt").string();
    REQUIRE(openads::engine::fs_rename(p, p2));
    REQUIRE(openads::engine::fs_erase(p2));
    auto ex = openads::engine::fs_exists(p2);
    REQUIRE(ex);
    CHECK_FALSE(ex.value());
}

TEST_CASE("server_fs: dir make list remove") {
    auto dir = fs::temp_directory_path() / "oads_server_fs_dir";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto sub = (dir / "inbox").string();
    REQUIRE(openads::engine::fs_dir_make(sub));
    auto fpath = (dir / "inbox" / "a.txt").string();
    {
        auto f = openads::engine::fs_open(
            fpath, openads::engine::ADS_FO_READWRITE, true);
        REQUIRE(f);
        f.value()->stream.write("x", 1);
    }
    auto list = openads::engine::fs_directory(sub, "*.txt");
    REQUIRE(list);
    CHECK(list.value().size() >= 1);
    REQUIRE(openads::engine::fs_erase(fpath));
    REQUIRE(openads::engine::fs_dir_remove(sub));
}

TEST_CASE("server_fs: pack/unpack dir entry") {
    openads::engine::DirEntry e;
    e.name = "hello.dbf";
    e.size = 42;
    e.year = 2026;
    e.mon = 7;
    e.day = 20;
    e.hh = 12;
    e.mm = 30;
    e.ss = 5;
    e.attr = 0;
    std::vector<std::uint8_t> buf;
    openads::engine::pack_dir_entry(e, buf);
    std::size_t off = 0;
    openads::engine::DirEntry o;
    REQUIRE(openads::engine::unpack_dir_entry(buf, off, o));
    CHECK(o.name == e.name);
    CHECK(o.size == e.size);
    CHECK(o.year == e.year);
}
