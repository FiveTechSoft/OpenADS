#include "doctest.h"
#include "engine/repl_apply.h"
#include "engine/repl_queue.h"
#include "engine/repl_catalog.h"
#include "engine/data_dict.h"
#include "engine/table.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::DataDict;
using openads::engine::ReplQueue;
using openads::engine::ReplRecType;
using openads::engine::ReplRecord;
using openads::engine::Table;
using openads::engine::TableType;
using openads::engine::OpenMode;

static void safe_remove(const fs::path& p) { std::error_code ec; fs::remove(p, ec); }

namespace {

std::vector<std::uint8_t> make_dbf_bytes(int record_count) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[1] = 124; hdr[2] = 1; hdr[3] = 31;
    hdr[4] = static_cast<std::uint8_t>(record_count & 0xFF);
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 5; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());

    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "ID", 11);
    fd[11] = 'C'; fd[16] = 5;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);

    for (int i = 0; i < record_count; ++i) {
        file.push_back(0x20);
        auto s = std::to_string(i + 1);
        for (int j = 0; j < 5; ++j)
            file.push_back(j < (int)s.size() ? static_cast<uint8_t>(s[j]) : 0x20);
    }
    file.push_back(0x1A);
    return file;
}

void write_dbf(const fs::path& dir, const char* name, int count) {
    auto p = dir / (std::string(name) + ".dbf");
    safe_remove(p);
    auto bytes = make_dbf_bytes(count);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

struct ReplTestFixture {
    fs::path add_path;
    fs::path tgt_dir;
    fs::path qpath;
    DataDict dd;

    ReplTestFixture(const char* tag) {
        add_path = fs::temp_directory_path() / (std::string("repl_apply_") + tag + ".add");
        tgt_dir = fs::temp_directory_path() / (std::string("repl_apply_tgt_") + tag);
        qpath = fs::temp_directory_path() / (std::string("repl_apply_") + tag + ".bin");
        std::error_code ec;
        fs::create_directories(tgt_dir, ec);
        safe_remove(add_path);
        safe_remove(qpath);
    }

    ~ReplTestFixture() {
        safe_remove(add_path);
        safe_remove(qpath);
        std::error_code ec;
        fs::remove_all(tgt_dir, ec);
    }

    void setup_dd(const char* table_name = "cust") {
        auto created = DataDict::create(add_path.string());
        REQUIRE(created.has_value());
        dd = std::move(created).value();
        REQUIRE(dd.add_table(table_name, (std::string(table_name) + ".dbf").c_str()).has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = table_name; art.identity_cols = {"ID"};
        REQUIRE(dd.create_article(art).has_value());

        DataDict::SubscriptionEntry sub;
        sub.name = "S1"; sub.publication = "P1";
        sub.target_uri = tgt_dir.string();
        REQUIRE(dd.create_subscription(sub).has_value());
    }

    ReplQueue open_queue() {
        ReplQueue q;
        REQUIRE(q.open(qpath.string()).has_value());
        return q;
    }
};

Table open_target(const fs::path& tgt_dir, const char* name = "cust") {
    auto opened = Table::open((tgt_dir / (std::string(name) + ".dbf")).string(), TableType::Cdx);
    REQUIRE(opened.has_value());
    return std::move(opened).value();
}

} // namespace

TEST_CASE("repl_apply_once: subscription not found") {
    auto p = fs::temp_directory_path() / "repl_apply_notfound.add";
    safe_remove(p);
    auto created = DataDict::create(p.string());
    REQUIRE(created.has_value());
    DataDict dd = std::move(created).value();
    auto result = openads::engine::repl_apply_once(dd, "", "nonexistent");
    CHECK_FALSE(result.has_value());
    safe_remove(p);
}

TEST_CASE("repl_apply_once: disabled subscription returns empty result") {
    ReplTestFixture f("disabled");
    f.setup_dd();
    DataDict::SubscriptionEntry sub;
    sub.name = "S2"; sub.publication = "P1";
    sub.target_uri = "/tmp"; sub.enabled = false;
    REQUIRE(f.dd.create_subscription(sub).has_value());

    auto result = openads::engine::repl_apply_once(f.dd, "", "S2");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 0);
}

TEST_CASE("repl_apply_once: empty queue returns zero") {
    ReplTestFixture f("emptyq");
    f.setup_dd();
    auto q = f.open_queue();

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 0);
}

TEST_CASE("repl_apply_once: INSERT applies to target table") {
    ReplTestFixture f("ins");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);
    CHECK(result.value().last_lsn_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 1);
    REQUIRE(t.goto_top().has_value());
    auto val = t.read_field(0);
    REQUIRE(val.has_value());
    CHECK(val.value().as_string == "AAA");
}

TEST_CASE("repl_apply_once: UPDATE applies to target table") {
    ReplTestFixture f("upd");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Update;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'Z', 'Z', 'Z', 'Z', 'Z'};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);
    CHECK(result.value().last_lsn_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);
    REQUIRE(t.goto_top().has_value());
    auto val = t.read_field(0);
    REQUIRE(val.has_value());
    CHECK(val.value().as_string == "ZZZZZ");
}

TEST_CASE("repl_apply_once: UPDATE preserves other records") {
    ReplTestFixture f("upd2");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Update;
        r.source_table = "cust";
        r.identity.push_back({"ID", "2"});
        r.after = {' ', 'M', 'O', 'D', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);

    REQUIRE(t.goto_top().has_value());
    auto v0 = t.read_field(0);
    REQUIRE(v0.has_value());
    CHECK(v0.value().as_string == "1");

    REQUIRE(t.skip(1).has_value());
    auto v1 = t.read_field(0);
    REQUIRE(v1.has_value());
    CHECK(v1.value().as_string == "MOD");

    REQUIRE(t.skip(1).has_value());
    auto v2 = t.read_field(0);
    REQUIRE(v2.has_value());
    CHECK(v2.value().as_string == "3");
}

TEST_CASE("repl_apply_once: DELETE applies to target table") {
    ReplTestFixture f("del");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Delete;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);
    CHECK(result.value().last_lsn_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);
    REQUIRE(t.goto_top().has_value());
    CHECK(t.is_deleted());
}

TEST_CASE("repl_apply_once: DELETE only removes target record") {
    ReplTestFixture f("del2");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Delete;
        r.source_table = "cust";
        r.identity.push_back({"ID", "2"});
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);

    REQUIRE(t.goto_top().has_value());
    CHECK_FALSE(t.is_deleted());

    REQUIRE(t.skip(1).has_value());
    CHECK(t.is_deleted());

    REQUIRE(t.skip(1).has_value());
    CHECK_FALSE(t.is_deleted());
}

TEST_CASE("repl_apply_once: multi-record INSERT") {
    ReplTestFixture f("multi");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        for (int i = 0; i < 3; ++i) {
            ReplRecord r;
            r.type = ReplRecType::Insert;
            r.source_table = "cust";
            r.identity.push_back({"ID", std::to_string(i + 1)});
            r.after = {' ', 'A', 'A', 'A', ' ', ' '};
            REQUIRE(q.append(r).has_value());
        }
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 3);
    CHECK(result.value().last_lsn_applied == 3);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);
}

TEST_CASE("repl_apply_once: mixed INSERT and UPDATE") {
    ReplTestFixture f("mixed");
    write_dbf(f.tgt_dir, "cust", 1);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r1;
        r1.type = ReplRecType::Insert;
        r1.source_table = "cust";
        r1.identity.push_back({"ID", "2"});
        r1.after = {' ', 'B', 'B', 'B', ' ', ' '};
        REQUIRE(q.append(r1).has_value());

        ReplRecord r2;
        r2.type = ReplRecType::Update;
        r2.source_table = "cust";
        r2.identity.push_back({"ID", "1"});
        r2.after = {' ', 'X', 'X', 'X', 'X', 'X'};
        REQUIRE(q.append(r2).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 2);
    CHECK(result.value().last_lsn_applied == 2);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 2);

    REQUIRE(t.goto_top().has_value());
    auto v0 = t.read_field(0);
    REQUIRE(v0.has_value());
    CHECK(v0.value().as_string == "XXXXX");

    REQUIRE(t.skip(1).has_value());
    auto v1 = t.read_field(0);
    REQUIRE(v1.has_value());
    CHECK(v1.value().as_string == "BBB");
}

TEST_CASE("repl_apply_once: mixed INSERT and DELETE") {
    ReplTestFixture f("mixed2");
    write_dbf(f.tgt_dir, "cust", 2);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r1;
        r1.type = ReplRecType::Insert;
        r1.source_table = "cust";
        r1.identity.push_back({"ID", "3"});
        r1.after = {' ', 'C', 'C', 'C', ' ', ' '};
        REQUIRE(q.append(r1).has_value());

        ReplRecord r2;
        r2.type = ReplRecType::Delete;
        r2.source_table = "cust";
        r2.identity.push_back({"ID", "1"});
        REQUIRE(q.append(r2).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 2);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);

    REQUIRE(t.goto_top().has_value());
    CHECK(t.is_deleted());

    REQUIRE(t.skip(1).has_value());
    CHECK_FALSE(t.is_deleted());

    REQUIRE(t.skip(1).has_value());
    CHECK_FALSE(t.is_deleted());
}

TEST_CASE("repl_apply_once: last_lsn persisted after apply") {
    ReplTestFixture f("lsn");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());

    auto reopened = DataDict::open(f.add_path.string());
    REQUIRE(reopened.has_value());
    DataDict dd2 = std::move(reopened).value();
    CHECK(dd2.subscriptions().at("S1").last_lsn == 1);
}

TEST_CASE("repl_apply_once: last_lsn advances across multiple records") {
    ReplTestFixture f("lsn2");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        for (int i = 0; i < 5; ++i) {
            ReplRecord r;
            r.type = ReplRecType::Insert;
            r.source_table = "cust";
            r.identity.push_back({"ID", std::to_string(i + 1)});
            r.after = {' ', 'A', 'A', 'A', ' ', ' '};
            REQUIRE(q.append(r).has_value());
        }
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 5);
    CHECK(result.value().last_lsn_applied == 5);

    auto reopened = DataDict::open(f.add_path.string());
    REQUIRE(reopened.has_value());
    DataDict dd2 = std::move(reopened).value();
    CHECK(dd2.subscriptions().at("S1").last_lsn == 5);
}

TEST_CASE("repl_apply_once: transaction markers are skipped but LSN advances") {
    ReplTestFixture f("txskip");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord tx_begin;
        tx_begin.type = ReplRecType::TxBegin;
        tx_begin.tx_id = 100;
        REQUIRE(q.append(tx_begin).has_value());

        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        r.tx_id = 100;
        REQUIRE(q.append(r).has_value());

        ReplRecord tx_commit;
        tx_commit.type = ReplRecType::TxCommit;
        tx_commit.tx_id = 100;
        REQUIRE(q.append(tx_commit).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);
    CHECK(result.value().last_lsn_applied == 3);

    auto reopened = DataDict::open(f.add_path.string());
    REQUIRE(reopened.has_value());
    DataDict dd2 = std::move(reopened).value();
    CHECK(dd2.subscriptions().at("S1").last_lsn == 3);
}

TEST_CASE("repl_apply_once: source table not in DD skips record") {
    ReplTestFixture f("notindd");
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "unknown_table";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 1);
}

TEST_CASE("repl_apply_once: target file not found skips record") {
    ReplTestFixture f("nofile");
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 1);
}

TEST_CASE("repl_apply_once: UPDATE on non-existent identity does nothing") {
    ReplTestFixture f("updmiss");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Update;
        r.source_table = "cust";
        r.identity.push_back({"ID", "999"});
        r.after = {' ', 'Z', 'Z', 'Z', 'Z', 'Z'};
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);
    REQUIRE(t.goto_top().has_value());
    auto val = t.read_field(0);
    REQUIRE(val.has_value());
    CHECK(val.value().as_string == "1");
}

TEST_CASE("repl_apply_once: DELETE on non-existent identity does nothing") {
    ReplTestFixture f("delmiss");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Delete;
        r.source_table = "cust";
        r.identity.push_back({"ID", "999"});
        REQUIRE(q.append(r).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 0);
    CHECK(result.value().last_lsn_applied == 1);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);
    REQUIRE(t.goto_top().has_value());
    CHECK_FALSE(t.is_deleted());
}

TEST_CASE("repl_apply_once: second call skips already-applied records") {
    ReplTestFixture f("idempotent");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        REQUIRE(q.append(r).has_value());
    }

    auto r1 = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(r1.has_value());
    CHECK(r1.value().records_applied == 1);
    CHECK(r1.value().last_lsn_applied == 1);

    auto r2 = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(r2.has_value());
    CHECK(r2.value().records_applied == 0);
    CHECK(r2.value().last_lsn_applied == 0);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 1);
}

TEST_CASE("repl_apply_once: TxAbort marker advances LSN without applying") {
    ReplTestFixture f("txabort");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord tx_begin;
        tx_begin.type = ReplRecType::TxBegin;
        tx_begin.tx_id = 200;
        REQUIRE(q.append(tx_begin).has_value());

        ReplRecord r;
        r.type = ReplRecType::Insert;
        r.source_table = "cust";
        r.identity.push_back({"ID", "1"});
        r.after = {' ', 'A', 'A', 'A', ' ', ' '};
        r.tx_id = 200;
        REQUIRE(q.append(r).has_value());

        ReplRecord tx_abort;
        tx_abort.type = ReplRecType::TxAbort;
        tx_abort.tx_id = 200;
        REQUIRE(q.append(tx_abort).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 1);
    CHECK(result.value().last_lsn_applied == 3);
}

TEST_CASE("repl_apply_once: UPDATE then DELETE on same record") {
    ReplTestFixture f("upddel");
    write_dbf(f.tgt_dir, "cust", 3);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r1;
        r1.type = ReplRecType::Update;
        r1.source_table = "cust";
        r1.identity.push_back({"ID", "2"});
        r1.after = {' ', 'U', 'P', 'D', ' ', ' '};
        REQUIRE(q.append(r1).has_value());

        ReplRecord r2;
        r2.type = ReplRecType::Delete;
        r2.source_table = "cust";
        r2.identity.push_back({"ID", "2"});
        REQUIRE(q.append(r2).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().last_lsn_applied == 2);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);

    REQUIRE(t.goto_top().has_value());
    CHECK_FALSE(t.is_deleted());

    REQUIRE(t.skip(1).has_value());
    CHECK_FALSE(t.is_deleted());
    auto v1 = t.read_field(0);
    REQUIRE(v1.has_value());
    CHECK(v1.value().as_string == "UPD");

    REQUIRE(t.skip(1).has_value());
    CHECK_FALSE(t.is_deleted());
}

TEST_CASE("repl_apply_once: large batch inserts") {
    ReplTestFixture f("largebatch");
    write_dbf(f.tgt_dir, "cust", 0);
    f.setup_dd();

    const int N = 50;
    {
        auto q = f.open_queue();
        for (int i = 0; i < N; ++i) {
            ReplRecord r;
            r.type = ReplRecType::Insert;
            r.source_table = "cust";
            r.identity.push_back({"ID", std::to_string(i + 1)});
            r.after = {' ', 'A', 'A', 'A', ' ', ' '};
            REQUIRE(q.append(r).has_value());
        }
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == N);
    CHECK(result.value().last_lsn_applied == static_cast<std::uint64_t>(N));

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == N);
}

TEST_CASE("repl_apply_once: UPDATE then INSERT interleaved") {
    ReplTestFixture f("interleave");
    write_dbf(f.tgt_dir, "cust", 2);
    f.setup_dd();

    {
        auto q = f.open_queue();
        ReplRecord r1;
        r1.type = ReplRecType::Update;
        r1.source_table = "cust";
        r1.identity.push_back({"ID", "1"});
        r1.after = {' ', 'U', 'U', 'U', 'U', 'U'};
        REQUIRE(q.append(r1).has_value());

        ReplRecord r2;
        r2.type = ReplRecType::Insert;
        r2.source_table = "cust";
        r2.identity.push_back({"ID", "3"});
        r2.after = {' ', 'I', 'I', 'I', 'I', 'I'};
        REQUIRE(q.append(r2).has_value());

        ReplRecord r3;
        r3.type = ReplRecType::Update;
        r3.source_table = "cust";
        r3.identity.push_back({"ID", "2"});
        r3.after = {' ', 'V', 'V', 'V', 'V', 'V'};
        REQUIRE(q.append(r3).has_value());
    }

    auto result = openads::engine::repl_apply_once(f.dd, f.qpath.string(), "S1");
    REQUIRE(result.has_value());
    CHECK(result.value().records_applied == 3);
    CHECK(result.value().last_lsn_applied == 3);

    Table t = open_target(f.tgt_dir);
    CHECK(t.record_count() == 3);

    REQUIRE(t.goto_top().has_value());
    auto v0 = t.read_field(0);
    REQUIRE(v0.has_value());
    CHECK(v0.value().as_string == "UUUUU");

    REQUIRE(t.skip(1).has_value());
    auto v1 = t.read_field(0);
    REQUIRE(v1.has_value());
    CHECK(v1.value().as_string == "VVVVV");

    REQUIRE(t.skip(1).has_value());
    auto v2 = t.read_field(0);
    REQUIRE(v2.has_value());
    CHECK(v2.value().as_string == "IIIII");
}
