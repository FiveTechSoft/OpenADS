#include "doctest.h"
#include "engine/backup.h"
#include "engine/data_dict.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace bk = openads::engine::backup;

namespace {

void write_file(const fs::path& p, const std::string& body) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << body;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

fs::path fresh_dir(const char* name) {
    std::error_code ec;
    fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

}  // namespace

TEST_CASE("backup options parser: SAP option string forms") {
    auto o = bk::parse_options(
        "include=t1, t2;MetaOnly;ArchiveFile=x.tar;Diff;bogus=1");
    REQUIRE(o.include.size() == 2);
    CHECK(o.include[0] == "t1");
    CHECK(o.include[1] == "t2");
    CHECK(o.meta_only);
    CHECK(!o.dont_overwrite);
    // ArchiveFile + Diff + unknown each produce a warning row.
    CHECK(o.warnings.size() == 3);

    auto empty = bk::parse_options("");
    CHECK(empty.warnings.empty());
    CHECK(!empty.meta_only);
}

TEST_CASE("free tables: backup + restore round-trip with masks/exclude") {
    auto src  = fresh_dir("openads_bk_free_src");
    auto dst  = fresh_dir("openads_bk_free_img");
    auto back = fresh_dir("openads_bk_free_rest");

    write_file(src / "a.dbf", "AAA");
    write_file(src / "a.cdx", "AIDX");
    write_file(src / "b.dbf", "BBB");
    write_file(src / "skip.txt", "not a table");

    bk::Options opt;
    auto r = bk::backup_free_tables(src.string(), "*.dbf",
                                    dst.string(), opt);
    REQUIRE(r.has_value());
    CHECK(r.value().files_copied == 3);           // a.dbf + a.cdx + b.dbf
    CHECK(read_file(dst / "a.dbf") == "AAA");
    CHECK(read_file(dst / "a.cdx") == "AIDX");    // companion followed
    CHECK(!fs::exists(dst / "skip.txt"));

    bk::Options ropt;
    ropt.exclude = {"b.dbf"};
    auto rr = bk::restore_free_tables(dst.string(), back.string(), ropt);
    REQUIRE(rr.has_value());
    CHECK(read_file(back / "a.dbf") == "AAA");
    CHECK(!fs::exists(back / "b.dbf"));           // excluded

    // DontOverwrite: a second restore warns for the files already present
    // (a.dbf + a.cdx) and copies only what's missing (the excluded b.dbf).
    bk::Options nopt;
    nopt.dont_overwrite = true;
    auto r2 = bk::restore_free_tables(dst.string(), back.string(), nopt);
    REQUIRE(r2.has_value());
    CHECK(r2.value().files_copied == 1);
    CHECK(r2.value().rows.size() == 2);
    CHECK(read_file(back / "b.dbf") == "BBB");
}

TEST_CASE("database: backup copies the dictionary and its bound tables") {
    auto src = fresh_dir("openads_bk_dd_src");
    auto img = fresh_dir("openads_bk_dd_img");
    auto rst = fresh_dir("openads_bk_dd_rst");

    auto ddres = openads::engine::DataDict::create(
        (src / "demo.add").string());
    REQUIRE(ddres.has_value());
    write_file(src / "cust.dbf", "CUSTDATA");
    write_file(src / "cust.cdx", "CUSTIDX");
    write_file(src / "inv.dbf",  "INVDATA");
    {
        auto dd = openads::engine::DataDict::open(
            (src / "demo.add").string());
        REQUIRE(dd.has_value());
        REQUIRE(dd.value().add_table("cust", "cust.dbf").has_value());
        REQUIRE(dd.value().add_table("inv",  "inv.dbf").has_value());
    }

    auto r = bk::backup_database((src / "demo.add").string(),
                                 img.string(), bk::Options{});
    REQUIRE(r.has_value());
    CHECK(fs::exists(img / "demo.add"));
    CHECK(read_file(img / "cust.dbf") == "CUSTDATA");
    CHECK(read_file(img / "cust.cdx") == "CUSTIDX");
    CHECK(read_file(img / "inv.dbf")  == "INVDATA");

    // MetaOnly: only the dictionary files come across.
    auto meta_img = fresh_dir("openads_bk_dd_meta");
    bk::Options mopt;
    mopt.meta_only = true;
    auto rm = bk::backup_database((src / "demo.add").string(),
                                  meta_img.string(), mopt);
    REQUIRE(rm.has_value());
    CHECK(fs::exists(meta_img / "demo.add"));
    CHECK(!fs::exists(meta_img / "cust.dbf"));

    // Restore into a new location, renaming the dictionary.
    auto rr = bk::restore_database((img / "demo.add").string(),
                                   (rst / "restored.add").string(),
                                   bk::Options{});
    REQUIRE(rr.has_value());
    CHECK(fs::exists(rst / "restored.add"));
    CHECK(read_file(rst / "cust.dbf") == "CUSTDATA");
    CHECK(read_file(rst / "inv.dbf")  == "INVDATA");
}

TEST_CASE("database backup: include list narrows the table set") {
    auto src = fresh_dir("openads_bk_dd_inc_src");
    auto img = fresh_dir("openads_bk_dd_inc_img");

    REQUIRE(openads::engine::DataDict::create(
        (src / "d.add").string()).has_value());
    write_file(src / "one.dbf", "1");
    write_file(src / "two.dbf", "2");
    {
        auto dd = openads::engine::DataDict::open((src / "d.add").string());
        REQUIRE(dd.has_value());
        REQUIRE(dd.value().add_table("one", "one.dbf").has_value());
        REQUIRE(dd.value().add_table("two", "two.dbf").has_value());
    }
    bk::Options opt;
    opt.include = {"one"};
    auto r = bk::backup_database((src / "d.add").string(),
                                 img.string(), opt);
    REQUIRE(r.has_value());
    CHECK(fs::exists(img / "one.dbf"));
    CHECK(!fs::exists(img / "two.dbf"));
}
