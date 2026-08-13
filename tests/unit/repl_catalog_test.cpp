#include "doctest.h"
#include "engine/data_dict.h"
#include "engine/repl_catalog.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::engine::DataDict;
using openads::engine::ReplCatalog;

static void safe_remove(const fs::path& p) { std::error_code ec; fs::remove(p, ec); }

TEST_CASE("DataDict: publication/article/subscription round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_roundtrip.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1"; pub.comment = "test pub";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = "cust";
        art.identity_cols = {"ID"};
        REQUIRE(dd.create_article(art).has_value());

        DataDict::SubscriptionEntry sub;
        sub.name = "S1"; sub.publication = "P1";
        sub.target_uri = "tcp://127.0.0.1:6262/x";
        REQUIRE(dd.create_subscription(sub).has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.publications().count("P1") == 1);
        CHECK(dd.publications().at("P1").comment == "test pub");
        CHECK(dd.articles().count("P1::A1") == 1);
        CHECK(dd.articles().at("P1::A1").source_table == "cust");
        CHECK(dd.articles().at("P1::A1").identity_cols.size() == 1);
        CHECK(dd.articles().at("P1::A1").identity_cols[0] == "ID");
        CHECK(dd.subscriptions().count("S1") == 1);
        CHECK(dd.subscriptions().at("S1").target_uri == "tcp://127.0.0.1:6262/x");
        CHECK(dd.subscriptions().at("S1").last_lsn == 0);
    }
    safe_remove(p);
}

TEST_CASE("ReplCatalog: table_is_published after article") {
    auto p = fs::temp_directory_path() / "openads_repl_catalog.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());
        REQUIRE(dd.add_table("orders", "orders.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = "cust"; art.identity_cols = {"ID"};
        REQUIRE(dd.create_article(art).has_value());

        ReplCatalog cat;
        cat.reload(dd);
        CHECK(cat.table_is_published("cust"));
        CHECK_FALSE(cat.table_is_published("orders"));

        auto arts = cat.articles_for_table("cust");
        REQUIRE(arts.size() == 1);
        CHECK(arts[0].name == "A1");

        // Drop article -> not published anymore
        REQUIRE(dd.drop_article("A1").has_value());
        cat.reload(dd);
        CHECK_FALSE(cat.table_is_published("cust"));
    }
    safe_remove(p);
}

TEST_CASE("create_article rejects empty identity") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_no_ident.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = "cust";
        // identity_cols empty
        CHECK_FALSE(dd.create_article(art).has_value());
    }
    safe_remove(p);
}

TEST_CASE("create_article rejects free table not in DD") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_no_table.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = "not_in_dd";
        art.identity_cols = {"K"};
        CHECK_FALSE(dd.create_article(art).has_value());
    }
    safe_remove(p);
}

TEST_CASE("drop_publication fails when a subscription remains") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_drop_pub.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::SubscriptionEntry sub;
        sub.name = "S1"; sub.publication = "P1";
        sub.target_uri = "tcp://127.0.0.1:6262/x";
        REQUIRE(dd.create_subscription(sub).has_value());

        // Cannot drop publication while subscription exists
        CHECK_FALSE(dd.drop_publication("P1").has_value());
        // Drop subscription first, then publication succeeds
        REQUIRE(dd.drop_subscription("S1").has_value());
        REQUIRE(dd.drop_publication("P1").has_value());
    }
    safe_remove(p);
}

TEST_CASE("set_subscription_last_lsn persists") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_lsn.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::SubscriptionEntry sub;
        sub.name = "S1"; sub.publication = "P1";
        sub.target_uri = "tcp://127.0.0.1:6262/x";
        REQUIRE(dd.create_subscription(sub).has_value());

        dd.set_subscription_last_lsn("S1", 42);
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.subscriptions().at("S1").last_lsn == 42);
    }
    safe_remove(p);
}

TEST_CASE("drop_publication cascades to articles") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_cascade.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("cust", "cust.dbf").has_value());

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());

        DataDict::ArticleEntry art;
        art.name = "A1"; art.publication = "P1";
        art.source_table = "cust"; art.identity_cols = {"ID"};
        REQUIRE(dd.create_article(art).has_value());

        REQUIRE(dd.articles().count("P1::A1") == 1);
        REQUIRE(dd.drop_publication("P1").has_value());
        CHECK(dd.articles().empty());
    }
    safe_remove(p);
}

TEST_CASE("duplicate publication name rejected") {
    auto p = fs::temp_directory_path() / "openads_dd_repl_dup_pub.add";
    safe_remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();

        DataDict::PublicationEntry pub;
        pub.name = "P1";
        REQUIRE(dd.create_publication(pub).has_value());
        CHECK_FALSE(dd.create_publication(pub).has_value());
    }
    safe_remove(p);
}
