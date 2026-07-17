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

// RCB 07/16/2026: column-level permissions — grant, per-op visibility, and
// save/reopen round-trip (parent table must survive). Foundation for closing
// the "OpenADS exposes columns SAP restricts" hole.
TEST_CASE("DataDict column-level permissions: grant + permitted_columns + round-trip") {
    using U = openads::engine::DataDict;
    auto p = fs::temp_directory_path() / "openads_dd_colperm.add";
    fs::remove(p);
    {
        auto created = U::create(p.string());
        REQUIRE(created.has_value());
        U dd = std::move(created).value();
        REQUIRE(dd.add_table("leases", "leases.adt").has_value());
        REQUIRE(dd.create_group("General").has_value());
        REQUIRE(dd.create_user("bob").has_value());
        REQUIRE(dd.add_user_to_group("bob", "General").has_value());

        // General may SELECT rent+tenant, and also UPDATE rent — but nothing
        // grants a column-level DELETE.
        REQUIRE(dd.grant_column_permission("leases", "rent",   "General",
                    U::DD_PERM_SELECT | U::DD_PERM_UPDATE).has_value());
        REQUIRE(dd.grant_column_permission("leases", "tenant", "General",
                    U::DD_PERM_SELECT).has_value());

        CHECK(dd.has_any_column_acl());
    }
    auto check = [](U& dd) {
        // bob (via General) can SELECT rent + tenant only.
        auto sel = dd.permitted_columns("bob", "leases", U::DD_PERM_SELECT);
        REQUIRE(sel.has_value());
        CHECK(sel->count("rent") == 1);
        CHECK(sel->count("tenant") == 1);
        CHECK(sel->count("deposit") == 0);   // not granted -> hidden
        // UPDATE is column-restricted to rent only.
        auto upd = dd.permitted_columns("bob", "leases", U::DD_PERM_UPDATE);
        REQUIRE(upd.has_value());
        CHECK(upd->count("rent") == 1);
        CHECK(upd->count("tenant") == 0);
        // DELETE has no column grant -> not column-restricted (table-level).
        CHECK_FALSE(dd.permitted_columns("bob", "leases", U::DD_PERM_DELETE).has_value());
        // A different table has no column restriction.
        CHECK_FALSE(dd.permitted_columns("bob", "other", U::DD_PERM_SELECT).has_value());
        // adssys is never column-restricted.
        CHECK_FALSE(dd.permitted_columns("adssys", "leases", U::DD_PERM_SELECT).has_value());
    };
    {
        auto opened = U::open(p.string());
        REQUIRE(opened.has_value());
        U dd = std::move(opened).value();
        CHECK(dd.has_any_column_acl());   // survived reopen
        check(dd);
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

// RCB 07/16/2026: SAP-binary Procedure records carry the SQL body in a
// STRUCTURED layout after the params: [le16 invoke_option][le32 reserved]
// [le16 body_len][body...]. The old parser scanned forward to the first
// CRLF to locate the body — but the first CRLF is the END of the body's
// FIRST LINE, so every imported proc silently lost its opening statement
// (pmsys sp_GetPhysicalPath lost "DECLARE @sql STRING;", breaking it).
// This fabricates a minimal SAP-binary .add with one Procedure record in
// that layout and asserts the body round-trips VERBATIM, first line included.
TEST_CASE("DataDict SAP-binary proc parse keeps the body's first line") {
    auto p = fs::temp_directory_path() / "openads_dd_sap_procbody.add";
    fs::remove(p);
    fs::remove(fs::temp_directory_path() / "openads_dd_sap_procbody.am");

    const std::string body =
        "  DECLARE @first STRING;\r\n"
        "  @first = 'line one must survive';\r\n"
        "  INSERT INTO __output VALUES (@first);";
    const std::string in_params = "a,CHAR,5;";

    constexpr std::size_t kHdr = 2200, kRec = 524;
    std::string buf(kHdr + kRec, '\0');
    std::memcpy(&buf[0], "ADS Data Dictionary", 19);
    auto p32at = [&](std::size_t off, std::uint32_t v) {
        buf[off]     = static_cast<char>( v        & 0xFF);
        buf[off + 1] = static_cast<char>((v >>  8) & 0xFF);
        buf[off + 2] = static_cast<char>((v >> 16) & 0xFF);
        buf[off + 3] = static_cast<char>((v >> 24) & 0xFF);
    };
    p32at(0x20, kHdr);
    p32at(0x24, kRec);

    std::size_t base = kHdr;
    buf[base] = 0x04;                                   // active
    p32at(base + 5, 1);                                 // obj_id
    std::memset(&buf[base + 13], ' ', 10);
    std::memcpy(&buf[base + 13], "Procedure", 9);       // obj_type
    std::memset(&buf[base + 23], ' ', 200);
    std::memcpy(&buf[base + 23], "sp_firstline", 12);   // obj_name

    // Property zone: [in_params\0][6x 0xFF][le16 invoke=4][le32 reserved]
    //                [le16 body_len][body]
    const std::uint16_t plen =
        static_cast<std::uint16_t>(in_params.size() + 1);
    buf[base + 223] = static_cast<char>(plen & 0xFF);
    buf[base + 224] = static_cast<char>((plen >> 8) & 0xFF);
    std::size_t pp = base + 225;
    std::memcpy(&buf[pp], in_params.data(), in_params.size());
    pp += plen;                                         // includes the NUL
    for (int i = 0; i < 6; ++i) buf[pp++] = static_cast<char>(0xFF);
    buf[pp++] = 0x04; buf[pp++] = 0x00;                 // invoke_option
    pp += 4;                                            // reserved le32 = 0..
    buf[pp - 4] = 0x04;                                 // ..matches SAP dumps
    const std::uint16_t blen = static_cast<std::uint16_t>(body.size());
    buf[pp++] = static_cast<char>(blen & 0xFF);
    buf[pp++] = static_cast<char>((blen >> 8) & 0xFF);
    std::memcpy(&buf[pp], body.data(), body.size());
    // more_property [498..506] stays zero: body is fully inline.

    std::ofstream(p, std::ios::binary).write(buf.data(),
        static_cast<std::streamsize>(buf.size()));

    auto opened = DataDict::open(p.string());
    REQUIRE(opened.has_value());
    DataDict dd = std::move(opened).value();
    REQUIRE(dd.has_proc("sp_firstline"));
    const auto& e = dd.procs().at("sp_firstline");
    CHECK(e.input_params == in_params);
    // The regression: the old CRLF scan dropped everything before the first
    // CRLF, i.e. the whole first line.
    CHECK(e.procedure == body);
    CHECK(e.procedure.find("DECLARE @first") != std::string::npos);

    fs::remove(p);
}
