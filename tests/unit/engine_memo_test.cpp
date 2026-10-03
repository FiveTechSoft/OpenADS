#include "doctest.h"
#include "drivers/dbt/dbt_memo.h"
#include "drivers/fpt/fpt_memo.h"
#include "engine/table.h"
#include "session/connection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace fs = std::filesystem;
using openads::drivers::MemoOpenMode;
using openads::drivers::dbt::DbtMemo;
using openads::drivers::fpt::FptMemo;
using openads::engine::OpenMode;
using openads::engine::Table;
using openads::engine::TableType;

namespace {

// DBF with one C(5) and one M(10) field, version 0x83 (with memo flag).
fs::path make_dbf_with_memo(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m4_memo_") + tag + ".dbf");
    fs::remove(p);
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x83;            // dBase III with memo
    hdr[4]  = 0;               // 0 records
    hdr[8]  = 32 + 64 + 1; hdr[9] = 0;     // header length: 32 + 2 fields*32 + 0x0D
    hdr[10] = 1 + 5 + 10; hdr[11] = 0;     // record length
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> name_fd{};
    std::strncpy(reinterpret_cast<char*>(name_fd.data()), "NAME", 11);
    name_fd[11] = 'C'; name_fd[16] = 5;
    file.insert(file.end(), name_fd.begin(), name_fd.end());

    std::array<std::uint8_t, 32> notes_fd{};
    std::strncpy(reinterpret_cast<char*>(notes_fd.data()), "NOTES", 11);
    notes_fd[11] = 'M'; notes_fd[16] = 10;
    file.insert(file.end(), notes_fd.begin(), notes_fd.end());

    file.push_back(0x0D);
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

} // namespace

TEST_CASE("Table writes + reads M-field through attached FPT memo") {
    auto dbf = make_dbf_with_memo("fpt");
    auto fpt = dbf; fpt.replace_extension(".fpt");
    fs::remove(fpt);
    {
        auto opened = Table::open(dbf.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();
        auto created = FptMemo::create(fpt.string(), 64);
        REQUIRE(created.has_value());
        t.attach_memo(std::make_unique<FptMemo>(std::move(created).value()));

        REQUIRE(t.append_record().has_value());
        REQUIRE(t.set_field(0, std::string("Anna")).has_value());
        REQUIRE(t.set_field(1, std::string("This is a long memo for Anna")).has_value());
        REQUIRE(t.flush().has_value());
    }
    {
        auto opened = Table::open(dbf.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();
        FptMemo m;
        REQUIRE(m.open(fpt.string(), MemoOpenMode::Shared).has_value());
        t.attach_memo(std::make_unique<FptMemo>(std::move(m)));

        REQUIRE(t.goto_top().has_value());
        auto v0 = t.read_field(0);
        REQUIRE(v0.has_value());
        CHECK(v0.value().as_string == "Anna");

        auto v1 = t.read_field(1);
        REQUIRE(v1.has_value());
        CHECK(v1.value().as_string == "This is a long memo for Anna");
    }
    fs::remove(dbf);
    fs::remove(fpt);
}

TEST_CASE("Table writes + reads M-field through attached DBT memo") {
    auto dbf = make_dbf_with_memo("dbt");
    auto dbt = dbf; dbt.replace_extension(".dbt");
    fs::remove(dbt);
    {
        auto opened = Table::open(dbf.string(), TableType::Ntx, OpenMode::Shared);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();
        auto created = DbtMemo::create(dbt.string());
        REQUIRE(created.has_value());
        t.attach_memo(std::make_unique<DbtMemo>(std::move(created).value()));

        REQUIRE(t.append_record().has_value());
        REQUIRE(t.set_field(0, std::string("Bob")).has_value());
        REQUIRE(t.set_field(1, std::string("Bob's notes here")).has_value());
        REQUIRE(t.flush().has_value());
    }
    {
        auto opened = Table::open(dbf.string(), TableType::Ntx, OpenMode::Shared);
        REQUIRE(opened.has_value());
        Table t = std::move(opened).value();
        DbtMemo m;
        REQUIRE(m.open(dbt.string(), MemoOpenMode::Shared).has_value());
        t.attach_memo(std::make_unique<DbtMemo>(std::move(m)));

        REQUIRE(t.goto_top().has_value());
        auto v1 = t.read_field(1);
        REQUIRE(v1.has_value());
        CHECK(v1.value().as_string == "Bob's notes here");
    }
    fs::remove(dbf);
    fs::remove(dbt);
}

TEST_CASE("remote table memo cap is checked in store before allocation; local reads remain available") {
    auto dbf = make_dbf_with_memo("remote_budget");
    auto fpt = dbf; fpt.replace_extension(".fpt");
    fs::remove(fpt);
    {
        auto opened = Table::open(dbf.string(), TableType::Cdx, OpenMode::Shared);
        REQUIRE(opened.has_value());
        Table table = std::move(opened).value();
        auto created = FptMemo::create(fpt.string(), 64);
        REQUIRE(created.has_value());
        table.attach_memo(std::make_unique<FptMemo>(std::move(created).value()));
        REQUIRE(table.append_record().has_value());
        REQUIRE(table.set_field(1, std::string(8u * 1024u * 1024u + 1u, 'X')).has_value());
        REQUIRE(table.flush().has_value());
        openads::session::Connection owner;
        owner.set_remote_server(true);
        table.set_owner(&owner);
        auto denied = table.read_field(1);
        REQUIRE_FALSE(denied.has_value());
        CHECK(denied.error().code == 7079);
        owner.set_remote_server(false);
        auto allowed = table.read_field(1);
        REQUIRE(allowed.has_value());
        CHECK(allowed.value().as_string.size() == 8u * 1024u * 1024u + 1u);
        table.set_owner(nullptr);
    }
    fs::remove(dbf);
    fs::remove(fpt);
}
