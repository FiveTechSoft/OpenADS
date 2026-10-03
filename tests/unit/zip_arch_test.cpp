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

TEST_CASE("zip: premature deflate EOF cannot count as successful extraction") {
    Scratch s;
    const auto source = s.src("a.dbf", "hello-zip");
    const auto archive = (s.dir / "early.zip").string();
    REQUIRE(za::zip_files({source}, (s.dir / "src").string(), archive, ZipOptions{}).has_value());
    // A valid short deflate stream with a larger declared length exercises
    // the same early-EOF path observed with random wrong-password bytes.
    // minizip skips CRC verification while remaining uncompressed bytes > 0.
    std::fstream file(archive, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(file.is_open());
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto central = bytes.find(std::string("PK\x01\x02", 4));
    REQUIRE(central != std::string::npos);
    REQUIRE(bytes.size() > central + 28);
    const char ten[4] = {10, 0, 0, 0};
    file.clear(); file.seekp(22); file.write(ten, 4);
    file.seekp(static_cast<std::streamoff>(central + 24)); file.write(ten, 4); file.close();
    const auto result = za::unzip_files(archive, (s.dir / "dst").string(), UnzipOptions{});
    CHECK_FALSE(result.has_value());
    CHECK_FALSE(fs::exists(s.dir / "dst" / "a.dbf"));
}

TEST_CASE("zip: existing destination symlinks cannot escape extraction root") {
    Scratch s;
    const auto outside = s.dir / "outside";
    fs::create_directories(outside);
    std::ofstream(outside / "keep.dbf") << "sentinel";
    auto source = s.src("keep.dbf", "replacement");
    auto archive = (s.dir / "escape.zip").string();
    REQUIRE(za::zip_files({source}, (s.dir / "src").string(), archive, ZipOptions{}).has_value());
    std::error_code error;
    fs::create_symlink(outside / "keep.dbf", s.dir / "dst" / "keep.dbf", error);
    if (error) { MESSAGE("symlink creation unavailable on this host"); return; }
    UnzipOptions options; options.overwrite = true;
    auto result = za::unzip_files(archive, (s.dir / "dst").string(), options);
    CHECK_FALSE(result.has_value());
    CHECK(s.read(outside / "keep.dbf") == "sentinel");
    fs::remove(s.dir / "dst" / "keep.dbf");
    std::ofstream(outside / "keep.dbf") << "sentinel";
    fs::create_directory_symlink(outside, s.dir / "dst" / "sub", error);
    REQUIRE_FALSE(error);
    source = s.src("sub/keep.dbf", "replacement");
    ZipOptions zip_options; zip_options.with_path = true; zip_options.overwrite = true;
    REQUIRE(za::zip_files({source}, (s.dir / "src").string(), archive, zip_options).has_value());
    options.with_path = true;
    result = za::unzip_files(archive, (s.dir / "dst").string(), options);
    CHECK_FALSE(result.has_value());
    CHECK(s.read(outside / "keep.dbf") == "sentinel");
}

TEST_CASE("zip: complete long entry names are preserved without truncated targets") {
    Scratch s;
    const auto archive = (s.dir / "long-name.zip").string();
    std::string name;
    for (int i = 0; i < 18; ++i) name += std::string(60, 'a') + "/";
    name += "a.dbf";
    zipFile zf = zipOpen64(archive.c_str(), APPEND_STATUS_CREATE);
    REQUIRE(zf != nullptr); zip_fileinfo zi{};
    REQUIRE(zipOpenNewFileInZip3_64(zf, name.c_str(), &zi, nullptr, 0, nullptr, 0,
        nullptr, 0, 0, 0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, nullptr, 0, 0) == ZIP_OK);
    REQUIRE(zipWriteInFileInZip(zf, "data", 4) == ZIP_OK);
    REQUIRE(zipCloseFileInZip(zf) == ZIP_OK); REQUIRE(zipClose(zf, nullptr) == ZIP_OK);
    auto entries = za::list_entries(archive); REQUIRE(entries.has_value());
    REQUIRE(entries.value().size() == 1);
    CHECK(entries.value()[0] == name);
    UnzipOptions options;
    auto result = za::unzip_files(archive, (s.dir / "dst").string(), options);
    REQUIRE(result.has_value());
    CHECK(s.read(s.dir / "dst" / "a.dbf") == "data");
}

TEST_CASE("zip: embedded NUL entry names cannot choose a shortened target") {
    Scratch s;
    const auto source = s.src("safeXevil.dbf", "data");
    const auto archive = (s.dir / "embedded-name.zip").string();
    REQUIRE(za::zip_files({source}, (s.dir / "src").string(), archive, ZipOptions{}).has_value());
    std::fstream file(archive, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(file.is_open());
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto central = bytes.find(std::string("PK\x01\x02", 4)); REQUIRE(central != std::string::npos);
    file.clear(); file.seekp(30 + 4); file.put('\0');
    file.seekp(static_cast<std::streamoff>(central + 46 + 4)); file.put('\0'); file.close();
    CHECK_FALSE(za::list_entries(archive).has_value());
    CHECK_FALSE(za::unzip_files(archive, (s.dir / "dst").string(), UnzipOptions{}).has_value());
    CHECK_FALSE(fs::exists(s.dir / "dst" / "safe"));
}

TEST_CASE("zip: explicit budgets bound creation and preserve rejected overwrite") {
    Scratch s;
    const auto a = s.src("a.dbf", "aaaa");
    const auto b = s.src("b.dbf", "bbbb");
    const auto arc = (s.dir / "budget.zip").string();
    ZipOptions opt;
    opt.limits.entries = 1;
    auto count = za::zip_files({a, b}, (s.dir / "src").string(), arc, opt);
    REQUIRE_FALSE(count);
    CHECK(count.error().code == openads::AE_ACCESS_DENIED);
    CHECK_FALSE(fs::exists(arc));
    opt.limits.entries = 2;
    opt.limits.bytes = 7;
    CHECK_FALSE(za::zip_files({a, b}, (s.dir / "src").string(), arc, opt));
    opt.limits.bytes = 8;
    opt.limits.metadata = 9; // two five-byte names
    CHECK_FALSE(za::zip_files({a, b}, (s.dir / "src").string(), arc, opt));
    opt.limits.metadata = 10;
    REQUIRE(za::zip_files({a, b}, (s.dir / "src").string(), arc, opt));
    const auto before = s.read(arc);
    opt.overwrite = true;
    opt.limits.bytes = 7;
    CHECK_FALSE(za::zip_files({a, b}, (s.dir / "src").string(), arc, opt));
    CHECK(s.read(arc) == before);
}

TEST_CASE("zip: extraction and both listings charge entries, bytes and metadata") {
    Scratch s;
    const auto a = s.src("a.dbf", std::string(4096, 'A'));
    const auto arc = (s.dir / "budget.zip").string();
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, ZipOptions{}));
    CHECK(fs::file_size(arc) < 4096);
    for (int kind = 0; kind < 3; ++kind) {
        za::Limits limits;
        if (kind == 0) limits.entries = 0;
        if (kind == 1) limits.bytes = 4095;
        if (kind == 2) limits.metadata = 68; // name plus per-entry accounting
        const auto names = za::list_entries(arc, limits);
        const auto details = za::list_detailed(arc, limits);
        CHECK_FALSE(names);
        CHECK_FALSE(details);
        UnzipOptions opt;
        opt.limits = limits;
        auto result = za::unzip_files(arc, (s.dir / "dst").string(), opt);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == openads::AE_ACCESS_DENIED);
        CHECK_FALSE(fs::exists(s.dir / "dst" / "a.dbf"));
    }
    za::Limits exact{1, 4096, 69, 0};
    REQUIRE(za::list_entries(arc, exact));
    REQUIRE(za::list_detailed(arc, exact));
    UnzipOptions opt;
    opt.limits = exact;
    REQUIRE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    CHECK(s.read(s.dir / "dst" / "a.dbf") == std::string(4096, 'A'));
}

TEST_CASE("zip: encrypted empty entries require a valid password header") {
    Scratch s;
    const auto a = s.src("empty.bin", "");
    const auto arc = (s.dir / "empty.zip").string();
    ZipOptions zip;
    zip.password = "correct";
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, zip));
    CHECK_FALSE(za::unzip_files(arc, (s.dir / "dst").string(), {}));
    CHECK_FALSE(fs::exists(s.dir / "dst" / "empty.bin"));
    UnzipOptions opt;
    opt.password = "correct";
    REQUIRE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    fs::remove(s.dir / "dst" / "empty.bin");
    // Change the last ciphertext header byte. With the correct password,
    // its plaintext verification byte must now differ, even with no payload.
    auto bytes = s.read(arc);
    REQUIRE(bytes.size() >= 42);
    const auto u16 = [&](std::size_t i) {
        return static_cast<unsigned char>(bytes[i]) |
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[i+1])) << 8);
    };
    const std::size_t header = 30 + u16(26) + u16(28);
    REQUIRE(header + 12 <= bytes.size());
    bytes[header + 11] ^= 1;
    {
        std::ofstream out(arc, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }
    CHECK_FALSE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    CHECK_FALSE(fs::exists(s.dir / "dst" / "empty.bin"));
}

TEST_CASE("zip: supplied password does not decrypt unencrypted entries") {
    Scratch s;
    const auto a = s.src("plain.bin", "plain content");
    const auto arc = (s.dir / "plain.zip").string();
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, {}));
    UnzipOptions opt;
    opt.password = "unused";
    REQUIRE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    CHECK(s.read(s.dir / "dst" / "plain.bin") == "plain content");
}

TEST_CASE("zip: descriptor flag selects DOS-time password verification byte") {
    Scratch s;
    const auto a = s.src("empty.bin", "");
    const auto arc = (s.dir / "descriptor.zip").string();
    ZipOptions zip;
    zip.password = "correct";
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, zip));
    auto bytes = s.read(arc);
    const auto cd = bytes.find(std::string("PK\x01\x02", 4));
    REQUIRE(cd != std::string::npos);
    REQUIRE(cd + 46 <= bytes.size());
    bytes[6] |= 8;
    bytes[cd + 8] |= 8;
    // Empty CRC is zero; the existing header check byte is zero.
    bytes[11] = 0;
    bytes[cd + 13] = 0;
    const auto save = [&] {
        std::ofstream out(arc, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    };
    save();
    UnzipOptions opt;
    opt.password = "correct";
    REQUIRE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    fs::remove(s.dir / "dst" / "empty.bin");
    bytes[11] = 1;
    bytes[cd + 13] = 1;
    save();
    CHECK_FALSE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    CHECK_FALSE(fs::exists(s.dir / "dst" / "empty.bin"));
}

TEST_CASE("zip: failed CRC overwrite preserves existing destination and cleans staging") {
    Scratch s;
    const auto a = s.src("a.dbf", "new contents");
    const auto arc = (s.dir / "corrupt.zip").string();
    REQUIRE(za::zip_files({a}, (s.dir / "src").string(), arc, {}));
    auto bytes = s.read(arc);
    const auto cd = bytes.find(std::string("PK\x01\x02", 4));
    REQUIRE(cd != std::string::npos);
    REQUIRE(cd + 20 <= bytes.size());
    // Matching local/central wrong CRC permits opening, then fails close/CRC.
    bytes[14] ^= 1;
    bytes[cd + 16] ^= 1;
    {
        std::ofstream out(arc, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }
    std::ofstream(s.dir / "dst" / "a.dbf", std::ios::binary) << "original";
    UnzipOptions opt;
    opt.overwrite = true;
    CHECK_FALSE(za::unzip_files(arc, (s.dir / "dst").string(), opt));
    CHECK(s.read(s.dir / "dst" / "a.dbf") == "original");
    for (const auto& e : fs::directory_iterator(s.dir / "dst"))
        CHECK(e.path().filename().string().find(".openads-zip-stage-") != 0);
}

TEST_CASE("zip: archive can replace a source only after reading its original bytes") {
    Scratch s;
    const auto arc = s.src("same.zip", "original archive bytes");
    ZipOptions opt;
    opt.overwrite = true;
    REQUIRE(za::zip_files({arc}, (s.dir / "src").string(), arc, opt));
    REQUIRE(za::unzip_files(arc, (s.dir / "dst").string(), {}));
    CHECK(s.read(s.dir / "dst" / "same.zip") == "original archive bytes");
    for (const auto& e : fs::directory_iterator(s.dir / "src"))
        CHECK(e.path().filename().string().find(".openads-zip-stage-") != 0);
}

TEST_CASE("zip: failed publish preserves a nonempty destination directory") {
    Scratch s;
    const auto a = s.src("a.dbf", "payload");
    const auto target = s.dir / "cannot-replace";
    fs::create_directory(target);
    std::ofstream(target / "sentinel") << "original";
    ZipOptions opt;
    opt.overwrite = true;
    CHECK_FALSE(za::zip_files({a}, (s.dir / "src").string(), target.string(), opt));
    CHECK(s.read(target / "sentinel") == "original");
    for (const auto& e : fs::directory_iterator(s.dir))
        CHECK(e.path().filename().string().find(".openads-zip-stage-") != 0);
}
