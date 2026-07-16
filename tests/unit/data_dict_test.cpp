#include "doctest.h"
#include "engine/data_dict.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using openads::engine::DataDict;

TEST_CASE("DataDict create + add_table + reopen + resolve") {
    auto p = fs::temp_directory_path() / "openads_m6_dd_basic.add";
    fs::remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("clientes", "clientes.dbf").has_value());
        REQUIRE(dd.add_table("ventas",   "v\\ventas.dbf").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_alias("clientes"));
        CHECK(dd.resolve("clientes")     == "clientes.dbf");
        CHECK(dd.resolve("ventas")       == "v\\ventas.dbf");
        CHECK(dd.resolve("not_an_alias") == "not_an_alias");
    }
    fs::remove(p);
}

// RCB 07/16/2026: per-tag index metadata (Name/expression/condition/options/
// key_length/collation) must survive a save/reopen, so system.indexes can
// report SAP-parity rows. Before enrichment the DD only stored index FILES.
TEST_CASE("DataDict add_index per-tag metadata round-trips") {
    auto p = fs::temp_directory_path() / "openads_dd_idx_meta.add";
    fs::remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("landlords", "landlords.adt").has_value());

        DataDict::IndexEntry e1;
        e1.table_alias = "landlords"; e1.index_path = "landlords.adi";
        e1.tag_name = "LANDLORDID"; e1.expression = "LandLordID";
        e1.options = "2051"; e1.key_length = "25";
        REQUIRE(dd.add_index(e1).has_value());

        // A second tag in the SAME file — must be a distinct entry, not a
        // dedup collision (they share index_path but differ by tag).
        DataDict::IndexEntry e2;
        e2.table_alias = "landlords"; e2.index_path = "landlords.adi";
        e2.tag_name = "BYCITY"; e2.expression = "City;Zip"; e2.condition = "inactive=0";
        e2.options = "2"; e2.key_length = "40";
        REQUIRE(dd.add_index(e2).has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        const auto& idx = dd.indexes();
        REQUIRE(idx.size() == 2);
        // Find each by tag and verify every per-tag field survived.
        auto find = [&](const std::string& tag) -> const DataDict::IndexEntry* {
            for (const auto& e : idx) if (e.tag_name == tag) return &e;
            return nullptr;
        };
        const auto* a = find("LANDLORDID");
        REQUIRE(a);
        CHECK(a->table_alias == "landlords");
        CHECK(a->expression  == "LandLordID");
        CHECK(a->options     == "2051");
        CHECK(a->key_length  == "25");
        const auto* b = find("BYCITY");
        REQUIRE(b);
        CHECK(b->expression == "City;Zip");
        CHECK(b->condition  == "inactive=0");
        CHECK(b->options    == "2");
    }
    fs::remove(p);
}

// -------------------------------------------------------------------------
// Legacy format rejection
// -------------------------------------------------------------------------

// Locate the pmsys.add SAP-binary fixture (may not exist on all machines).
static fs::path pmsys_fixture() {
    auto primary = fs::path(__FILE__).parent_path().parent_path().parent_path()
                   / "testdata" / "pmsys" / "pmsys.add";
    if (fs::exists(primary)) return primary;
    return fs::path(__FILE__).parent_path().parent_path()
           / "fixtures" / "adi" / "pmsys.add";
}

TEST_CASE("DataDict open — SAP ADS binary .add — loads with sap_permissions flag") {
    // SAP binary .add files are readable by DataDict::open() (needed by the
    // import tool).  They must load successfully and report has_sap_permissions()
    // so that AdsConnect60 can block normal connections to them.
    auto fixture = pmsys_fixture();
    if (!fs::exists(fixture)) {
        WARN("pmsys.add fixture not found, skipping SAP-binary test");
        return;
    }
    auto opened = DataDict::open(fixture.string());
    REQUIRE(opened.has_value());
    CHECK(opened.value().has_sap_permissions());
}

// -------------------------------------------------------------------------
// New-format mutation round-trip tests (using DataDict::create)
// -------------------------------------------------------------------------

TEST_CASE("DataDict create_user + delete_user round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_user_roundtrip.add";
    fs::remove(p);
    {
        auto cr = DataDict::create(p.string());
        REQUIRE(cr.has_value());
        DataDict dd = std::move(cr).value();
        REQUIRE(dd.create_user("newuser_crud").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_user("newuser_crud"));
        REQUIRE(dd.delete_user("newuser_crud").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.has_user("newuser_crud"));
    }
    fs::remove(p);
}

TEST_CASE("DataDict create_group + add_user_to_group round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_grp_roundtrip.add";
    fs::remove(p);
    {
        auto cr = DataDict::create(p.string());
        REQUIRE(cr.has_value());
        DataDict dd = std::move(cr).value();
        REQUIRE(dd.create_group("newgrp").has_value());
        REQUIRE(dd.add_user_to_group("user1", "newgrp").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_group("newgrp"));
        CHECK(dd.is_member_of("user1", "newgrp"));
    }
    fs::remove(p);
}

TEST_CASE("DataDict remove_user_from_group round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_grprm_roundtrip.add";
    fs::remove(p);
    {
        auto cr = DataDict::create(p.string());
        REQUIRE(cr.has_value());
        DataDict dd = std::move(cr).value();
        REQUIRE(dd.create_group("tmpgrp").has_value());
        REQUIRE(dd.add_user_to_group("user2", "tmpgrp").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        REQUIRE(dd.is_member_of("user2", "tmpgrp"));
        REQUIRE(dd.remove_user_from_group("user2", "tmpgrp").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.is_member_of("user2", "tmpgrp"));
    }
    fs::remove(p);
}

TEST_CASE("DataDict DB: built-in group membership round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_dbgrp_roundtrip.add";
    fs::remove(p);
    {
        auto cr = DataDict::create(p.string());
        REQUIRE(cr.has_value());
        DataDict dd = std::move(cr).value();
        REQUIRE(dd.add_user_to_group("user-admin",  "DB:Admin").has_value());
        REQUIRE(dd.add_user_to_group("user-backup", "DB:Backup").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.has_group("DB:Admin"));
        CHECK(dd.has_group("DB:Backup"));
        CHECK(dd.is_member_of("user-admin",  "DB:Admin"));
        CHECK(dd.is_member_of("user-backup", "DB:Backup"));
        REQUIRE(dd.add_user_to_group("user-debug", "DB:Admin").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.is_member_of("user-admin",  "DB:Admin"));
        CHECK(dd.is_member_of("user-debug",  "DB:Admin"));
        CHECK(dd.is_member_of("user-backup", "DB:Backup"));
    }
    fs::remove(p);
}

TEST_CASE("DataDict grant_permission round-trip") {
    auto p = fs::temp_directory_path() / "openads_dd_perm_roundtrip.add";
    fs::remove(p);
    {
        auto cr = DataDict::create(p.string());
        REQUIRE(cr.has_value());
        DataDict dd = std::move(cr).value();
        REQUIRE(dd.add_table("landlords", ".\\landlords.adt").has_value());
        REQUIRE(dd.grant_permission("Table", "landlords", "user-public", 0x001u).has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.get_effective_permission("user-public", "landlords") == 1);
        REQUIRE(dd.grant_permission("Table", "landlords", "user-public", 0x013u).has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.get_effective_permission("user-public", "landlords") == 2);
        REQUIRE(dd.set_table_permission("landlords", "user-admin", 3).has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK(dd.get_effective_permission("user-admin", "landlords") == 3);
    }
    fs::remove(p);
}

TEST_CASE("DataDict remove_table + reopen no longer has the alias") {
    auto p = fs::temp_directory_path() / "openads_m6_dd_remove.add";
    fs::remove(p);
    {
        auto created = DataDict::create(p.string());
        REQUIRE(created.has_value());
        DataDict dd = std::move(created).value();
        REQUIRE(dd.add_table("a", "a.dbf").has_value());
        REQUIRE(dd.add_table("b", "b.dbf").has_value());
        REQUIRE(dd.remove_table("a").has_value());
    }
    {
        auto opened = DataDict::open(p.string());
        REQUIRE(opened.has_value());
        DataDict dd = std::move(opened).value();
        CHECK_FALSE(dd.has_alias("a"));
        CHECK(dd.has_alias("b"));
    }
    fs::remove(p);
}
