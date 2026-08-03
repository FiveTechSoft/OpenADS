#include "doctest.h"
#include "session/connection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::TableType;
using openads::session::Connection;

namespace {

fs::path tmp_dir(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m1_conn_") + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

void write_minimal_dbf(const fs::path& p) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 1;
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 3; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C';
    fd[16] = 3;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' '); file.push_back('a'); file.push_back('b'); file.push_back('c');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

} // namespace

TEST_CASE("Connection opens against a directory") {
    auto dir = tmp_dir("open");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Connection opens a CDX-typed table by relative name") {
    auto dir = tmp_dir("table");
    write_minimal_dbf(dir / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();

        auto th = c.open_table("data.dbf", TableType::Cdx);
        REQUIRE(th.has_value());
        auto* table = c.lookup_table(th.value());
        REQUIRE(table != nullptr);
        CHECK(table->field_count() == 1);
        CHECK(table->record_count() == 1);

        c.close_table(th.value());
        CHECK(c.lookup_table(th.value()) == nullptr);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// openads_serverd --legacy-paths: a legacy ERP opens tables by absolute
// local path (USE "E:\CLIENT\FILE.DBF"). With legacy mode on, the
// connection strips the drive letter and matches its own data directory
// case-insensitively, so the same USE line resolves to the real file.
TEST_CASE("Connection legacy_paths resolves a foreign-drive absolute path") {
    auto dir = tmp_dir("legacy");
    fs::create_directories(dir / "Sub");
    write_minimal_dbf(dir / "Sub" / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_legacy_paths(true);

        // Spell the very same file through a drive letter that does not
        // exist on this machine — the verbatim-open probe must miss and
        // the legacy prefix-strip (drive-insensitive) must land it.
        const std::string abs = (dir / "Sub" / "data.dbf").generic_string();
        std::string foreign = "E:";
        foreign += (abs.size() >= 2 && abs[1] == ':') ? abs.substr(2) : abs;
        const std::string want =
            fs::weakly_canonical(dir / "Sub" / "data.dbf").generic_string();

        auto type = TableType::Cdx;
        CHECK(fs::path(c.resolve_table_file(foreign, type))
                  .generic_string() == want);

        // Same path with legacy mode OFF folds the whole remainder under
        // the data directory and does NOT find the file.
        auto opened2 = Connection::open(dir.string());
        REQUIRE(opened2.has_value());
        Connection c2 = std::move(opened2).value();
        auto type2 = TableType::Cdx;
        const std::string off = c2.resolve_table_file(foreign, type2);
        CHECK(off != want);
        CHECK_FALSE(fs::exists(off));

        // End to end: the foreign path opens the table.
        auto th = c.open_table(foreign, TableType::Cdx);
        REQUIRE(th.has_value());
        auto* table = c.lookup_table(th.value());
        REQUIRE(table != nullptr);
        CHECK(table->record_count() == 1);
        c.close_table(th.value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Connection legacy_paths folds an unknown absolute path under root") {
    auto dir = tmp_dir("legacyfold");
    fs::create_directories(dir / "CLIENTFOLD");
    write_minimal_dbf(dir / "CLIENTFOLD" / "fold.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_legacy_paths(true);

        // No root prefix in common: drive dropped, remainder joined —
        // this is what serves several ERP client folders from one root.
        auto type = TableType::Cdx;
        const std::string want =
            fs::weakly_canonical(dir / "CLIENTFOLD" / "fold.dbf")
                .generic_string();
        CHECK(fs::path(c.resolve_table_file("E:\\CLIENTFOLD\\fold.dbf", type))
                  .generic_string() == want);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
