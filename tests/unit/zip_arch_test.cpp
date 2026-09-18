// Server-side ZIP/UNZIP engine tests (no server needed — pure
// filesystem + minizip round-trips through engine::zip_arch).

#include "doctest.h"
#include "engine/zip_arch.h"
#include "openads/error.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "zip.h"

namespace fs = std::filesystem;
using openads::engine::zip_arch::Stats;
using openads::engine::zip_arch::UnzipOptions;
using openads::engine::zip_arch::ZipOptions;
namespace za = openads::engine::zip_arch;

namespace {

struct Scratch {
    fs::path dir;
    Scratch() {
        dir = fs::temp_directory_path() / "openads_zip_arch";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "src", ec);
        fs::create_directories(dir / "dst", ec);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string src(const char* name, const std::string& data) {
        auto p = dir / "src" / name;
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary).write(data.data(),
            static_cast<std::streamsize>(data.size()));
        return p.string();
    }
    std::string read(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }
};

}  // namespace

TEST_CASE("zip: roundtrip preserves bytes, stats add up") {
    Scratch s;
    const std::string a = s.src("a.dbf", std::string(3000, 'A'));
    const std::string b = s.src("sub/b.dbf", std::string(100, 'B'));
    const std::string arc = (s.dir / "arc.zip").string();

    ZipOptions opt;
    opt.with_path = true;
    auto zr = za::zip_files({a, b}, (s.dir / "src").string(), arc, opt);
    if (!(zr).has_value()) FAIL((zr).error().message);
    CHECK(zr.value().files == 2u);
    CHECK(zr.value().bytes == 3100u);
    CHECK(zr.value().archive_bytes > 0u);
    CHECK(zr.value().archive_bytes < zr.value().bytes);  // deflated

    UnzipOptions uo;
    uo.with_path = true;
    auto ur = za::unzip_files(arc, (s.dir / "dst").string(), uo);
    if (!(ur).has_value()) FAIL((ur).error().message);
    CHECK(ur.value().files == 2u);
    CHECK(ur.value().bytes == 3100u);
    CHECK(s.read(s.dir / "dst" / "a.dbf") == std::string(3000, 'A'));
    CHECK(s.read(s.dir / "dst" / "sub" / "b.dbf") ==
          std::string(100, 'B'));
}

TEST_CASE("zip: flat names by default, password roundtrip") {
    Scratch s;
    const std::string a = s.src("sub/a.dbf", "hello-zip");
    const std::string arc = (s.dir / "arc.zip").string();

    ZipOptions opt;
    opt.password = "s3cret";
    auto zr = za::zip_files({a}, (s.dir / "src").string(), arc, opt);
    if (!(zr).has_value()) FAIL((zr).error().message);

    // Wrong password fails loud (not silent garbage).
    UnzipOptions bad;
    bad.password = "nope";
    auto br = za::unzip_files(arc, (s.dir / "dst").string(), bad);
    CHECK(!br);

    UnzipOptions uo;
    uo.password = "s3cret";
    auto ur = za::unzip_files(arc, (s.dir / "dst").string(), uo);
    if (!(ur).has_value()) FAIL((ur).error().message);
    CHECK(s.read(s.dir / "dst" / "a.dbf") == "hello-zip");
}

TEST_CASE("zip: overwrite, missing and empty rules are loud") {
    Scratch s;
    const std::string a = s.src("a.dbf", "x");
    const std::string arc = (s.dir / "arc.zip").string();
    ZipOptions opt;

    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, opt));
    // Second time without overwrite: loud refusal.
    auto twice =
        za::zip_files({a}, (s.dir / "src").string(), arc, opt);
    CHECK(!twice);
    CHECK(twice.error().code == openads::AE_INTERNAL_ERROR);
    // With overwrite: fine.
    opt.overwrite = true;
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, opt));

    // Missing source names the file.
    auto miss = za::zip_files({(s.dir / "src" / "ghost.dbf").string()},
                              (s.dir / "src").string(),
                              (s.dir / "m.zip").string(), ZipOptions{});
    CHECK(!miss);
    CHECK(miss.error().code == openads::AE_NO_FILE_FOUND);

    // Empty list fails (no empty archives).
    auto empty = za::zip_files({}, (s.dir / "src").string(),
                               (s.dir / "e.zip").string(), ZipOptions{});
    CHECK(!empty);

    // Unzip of nothing fails loud.
    auto noarc = za::unzip_files((s.dir / "no.zip").string(),
                                 (s.dir / "dst").string(),
                                 UnzipOptions{});
    CHECK(!noarc);
    CHECK(noarc.error().code == openads::AE_NO_FILE_FOUND);
}

TEST_CASE("zip: excludes and flat-collision guard") {    Scratch s;
    const std::string a = s.src("a.dbf", "A");
    s.src("skipme.tmp", "TMP");
    const std::string arc = (s.dir / "arc.zip").string();

    ZipOptions opt;
    opt.exclude = {"*.tmp"};
    auto zr = za::zip_files({a, (s.dir / "src" / "skipme.tmp").string()},
                            (s.dir / "src").string(), arc, opt);
    if (!(zr).has_value()) FAIL((zr).error().message);
    CHECK(zr.value().files == 1u);

    // Flat extraction of a with-path archive collides loudly when
    // two entries share a basename.
    const std::string c1 = s.src("d1/dup.dbf", "1");
    const std::string c2 = s.src("d2/dup.dbf", "22");
    ZipOptions wp;
    wp.with_path = true;
    const std::string arc2 = (s.dir / "arc2.zip").string();
    REQUIRE(za::zip_files({c1, c2}, (s.dir / "src").string(), arc2,
                          wp));
    auto flat = za::unzip_files(arc2, (s.dir / "dst").string(),
                                UnzipOptions{});
    CHECK(!flat);
}

TEST_CASE("zip: Zip-Slip entries rejected, nothing escapes") {
    // Craft a hostile archive directly (bypasses zip_files, which
    // never emits such names) and prove extraction refuses it.
    Scratch s;
    const std::string arc = (s.dir / "evil.zip").string();
    {
        zipFile zf = zipOpen64(arc.c_str(), APPEND_STATUS_CREATE);
        REQUIRE(zf != nullptr);
        zip_fileinfo zi{};
        REQUIRE(zipOpenNewFileInZip3_64(zf, "../../evil.txt", &zi,
                                        nullptr, 0, nullptr, 0, nullptr,
                                        0, 0, 0, -MAX_WBITS,
                                        DEF_MEM_LEVEL,
                                        Z_DEFAULT_STRATEGY, nullptr, 0,
                                        0) == ZIP_OK);
        const char* payload = "pwned";
        REQUIRE(zipWriteInFileInZip(zf, payload, 5) == ZIP_OK);
        REQUIRE(zipCloseFileInZip(zf) == ZIP_OK);
        REQUIRE(zipClose(zf, nullptr) == ZIP_OK);
    }
    auto ur = za::unzip_files(arc, (s.dir / "dst").string(),
                              UnzipOptions{});
    CHECK(!ur);
    CHECK(ur.error().code == openads::AE_ACCESS_DENIED);
    CHECK(!fs::exists(s.dir / "evil.txt"));
    CHECK(!fs::exists(s.dir / "dst" / "evil.txt"));
}
