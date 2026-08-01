// S2 integration tests — DD SQL-script stored procedures + trigger error
// propagation through the script engine (docs/script-engine.md §4.3/§4.4).
//
// RCB 07/17/2026: semantics oracle-verified on SAP ADS 11 (probe Q6, doc
// §10): a failing trigger makes the DML return an error; BEFORE failure
// blocks the write, AFTER failure keeps it. Script procs read inputs via
// "(SELECT param FROM __input)" and produce their result set by inserting
// into __output.
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "test_dd_make.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Minimal DBF: one numeric field "ID" N(4), zero records.
fs::path make_ids_dbf(const fs::path& dir, const char* leaf) {
    fs::create_directories(dir);
    auto p = dir / leaf;
    fs::remove(p);
    std::vector<std::uint8_t> f;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    const std::uint16_t hsize = 32 + 32 + 1;
    hdr[8] = hsize & 0xFF; hdr[9] = (hsize >> 8) & 0xFF;
    hdr[10] = 1 + 4;
    f.insert(f.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "ID", 11);
    fd[11] = 'N'; fd[16] = 4; fd[17] = 0;
    f.insert(f.end(), fd.begin(), fd.end());
    f.push_back(0x0D);
    f.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(f.data()),
        static_cast<std::streamsize>(f.size()));
    return p;
}

struct SpFixture {
    fs::path  dir;
    ADSHANDLE hConn = 0;
    ADSHANDLE hStmt = 0;

    explicit SpFixture(const char* leaf) {
        dir = fs::temp_directory_path() / leaf;
        std::error_code ec;
        fs::remove_all(dir, ec);
        make_ids_dbf(dir, "orders.dbf");
        make_ids_dbf(dir, "audit.dbf");
        auto add = dir / "sp.add";
        openads_test::make_dd(add,
            "TABLE orders=orders.dbf\n"
            "TABLE audit=audit.dbf\n");
        auto s = add.string();
        UNSIGNED8 buf[512];
        std::memcpy(buf, s.c_str(), s.size() + 1);
        REQUIRE(AdsConnect60(buf, ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &hConn) == 0);
        REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    }
    ~SpFixture() {
        if (hStmt) AdsCloseSQLStatement(hStmt);
        if (hConn) AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    std::string last_msg;   // AdsGetLastError text of the last failed run()

    UNSIGNED32 run(const std::string& sql, ADSHANDLE* out = nullptr) {
        std::vector<UNSIGNED8> sb(sql.size() + 1);
        std::memcpy(sb.data(), sql.c_str(), sql.size() + 1);
        ADSHANDLE hc = 0;
        UNSIGNED32 rc = AdsExecuteSQLDirect(hStmt, sb.data(), &hc);
        if (rc != 0) {
            UNSIGNED32 ec = 0;
            char em[512] = {0};
            UNSIGNED16 el = sizeof(em) - 1;
            AdsGetLastError(&ec, reinterpret_cast<UNSIGNED8*>(em), &el);
            last_msg = std::string(em, el < sizeof(em) ? el : 0);
        }
        if (out) *out = hc;
        else if (hc) AdsCloseTable(hc);
        return rc;
    }
    // First field of the first row, trimmed on both sides (numeric DBF
    // fields come back right-aligned).
    std::string scalar(const std::string& sql) {
        ADSHANDLE hc = 0;
        if (run(sql, &hc) != 0 || hc == 0) return "<err:" + last_msg + ">";
        UNSIGNED8 nm[128] = {0};
        UNSIGNED16 cap = sizeof(nm) - 1;
        AdsGetFieldName(hc, 1, nm, &cap);
        nm[cap] = 0;
        std::vector<char> vb(4096, 0);
        UNSIGNED32 vl = static_cast<UNSIGNED32>(vb.size() - 1);
        AdsGetString(hc, nm, reinterpret_cast<UNSIGNED8*>(vb.data()), &vl, 0);
        AdsCloseTable(hc);
        std::string v(vb.data(), vl);
        while (!v.empty() && v.back() == ' ') v.pop_back();
        auto b = v.find_first_not_of(' ');
        if (b != std::string::npos) v = v.substr(b);
        else v.clear();
        return v;
    }
    void create_proc(const char* name, const char* body,
                     const char* input, const char* output) {
        REQUIRE(AdsDDCreateProcedure(
            hConn, (UNSIGNED8*)name, nullptr, (UNSIGNED8*)body, 0,
            (UNSIGNED8*)input, (UNSIGNED8*)output, nullptr) == 0);
    }
    // type: combined ADS_BEFORE_*/ADS_AFTER_* trigger constants.
    void create_trigger(const char* name, const char* table,
                        UNSIGNED32 type, const char* body) {
        REQUIRE(AdsDDCreateTrigger(hConn, (UNSIGNED8*)name,
            (UNSIGNED8*)table, type, 0, (UNSIGNED8*)body, nullptr, 5) == 0);
    }
};

}  // namespace

TEST_CASE("script proc: __output insert becomes the result cursor") {
    SpFixture f("openads_sp_out");
    f.create_proc("sp_hello",
                  "INSERT INTO __output(msg) VALUES ('hello world');",
                  "", "msg,CHAR,20;");
    CHECK(f.scalar("EXECUTE PROCEDURE sp_hello()") == "hello world");
}

// ---------------------------------------------------------------------------
// A declared output column name must reach the caller in full.
//
// The __output temp used to be created as a DBF free table, and a DBF field
// descriptor allots the name 11 bytes — so every stored-procedure result column
// was silently cut to 10 characters (sp_GetPhysicalPath's `databasepath` came
// back as `databasepa`). That capped the whole SQL engine at 10-character
// result columns for no reason: procedures read ADT tables and declare outputs
// mirroring SAP catalog columns, whose names run to 18. A caller that then
// referenced the column by name simply could not find it.
//
// The temp is now ADT. If this fails with a truncated name, a materialising
// path went back to a DBF-format temp — see docs/materialised-cursor-temps.md.
// Do not "fix" the expectation.
// ---------------------------------------------------------------------------
TEST_CASE("script proc: __output keeps column names longer than 10 chars") {
    SpFixture f("openads_sp_longcol");
    // 17 and 12 characters: both past DBF's limit, both real SAP widths
    // (User_Defined_Prop is a system.users column; databasepath is what
    // sp_GetPhysicalPath declares).
    f.create_proc("sp_longcols",
                  "INSERT INTO __output(User_Defined_Prop, databasepath) "
                  "VALUES ('abc', 'xyz');",
                  "", "User_Defined_Prop,CHAR,20;databasepath,CHAR,20;");

    ADSHANDLE hc = 0;
    REQUIRE(f.run("EXECUTE PROCEDURE sp_longcols()", &hc) == 0);
    REQUIRE(hc != 0);

    UNSIGNED16 count = 0;
    REQUIRE(AdsGetNumFields(hc, &count) == 0);
    REQUIRE(count == 2);

    std::vector<std::string> names;
    for (UNSIGNED16 i = 1; i <= count; ++i) {
        UNSIGNED8  nm[128] = {0};
        UNSIGNED16 cap = sizeof(nm) - 1;
        REQUIRE(AdsGetFieldName(hc, i, nm, &cap) == 0);
        std::string got(reinterpret_cast<char*>(nm), cap);
        while (!got.empty() && (got.back() == ' ' || got.back() == '\0'))
            got.pop_back();
        names.push_back(got);
    }
    CAPTURE(names[0]);
    CAPTURE(names[1]);
    CHECK(names[0] == "User_Defined_Prop");
    CHECK(names[1] == "databasepath");

    // ...and the value is still reachable BY that full name, which is the
    // thing a truncated descriptor actually breaks for callers.
    UNSIGNED8  fld[32] = "User_Defined_Prop";
    UNSIGNED8  vb[64]  = {0};
    UNSIGNED32 vl      = sizeof(vb) - 1;
    REQUIRE(AdsGetString(hc, fld, vb, &vl, 0) == 0);
    std::string v(reinterpret_cast<char*>(vb), vl);
    while (!v.empty() && v.back() == ' ') v.pop_back();
    CHECK(v == "abc");

    AdsCloseTable(hc);
}

TEST_CASE("script proc: reads __input params, writes a real table") {
    SpFixture f("openads_sp_in");
    f.create_proc("sp_add",
                  "DECLARE @k INTEGER; "
                  "@k = (SELECT n FROM __input); "
                  "INSERT INTO orders(ID) VALUES (@k);",
                  "n,INTEGER;", "");
    UNSIGNED32 rc = f.run("EXECUTE PROCEDURE sp_add(42)");
    INFO("sp_add error: " << f.last_msg);
    CHECK(rc == 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM orders") == "1");
    CHECK(f.scalar("SELECT ID FROM orders") == "42");
}

TEST_CASE("script proc: control flow + direct param name + SET form") {
    SpFixture f("openads_sp_flow");
    f.create_proc("sp_flow",
                  "DECLARE @r CHAR(10); "
                  "IF n > 5 THEN SET @r = 'big'; ELSE @r = 'small'; ENDIF; "
                  "INSERT INTO __output(verdict) VALUES (@r);",
                  "n,INTEGER;", "verdict,CHAR,10;");
    CHECK(f.scalar("EXECUTE PROCEDURE sp_flow(9)") == "big");
    CHECK(f.scalar("EXECUTE PROCEDURE sp_flow(3)") == "small");
}

TEST_CASE("script proc: error propagates as statement failure") {
    SpFixture f("openads_sp_err");
    f.create_proc("sp_boom", "RAISE oops(901, 'proc failed');", "", "");
    CHECK(f.run("EXECUTE PROCEDURE sp_boom()") != 0);
}

TEST_CASE("trigger: failing BEFORE blocks the write (oracle probe Q6)") {
    SpFixture f("openads_trg_before");
    f.create_trigger("t1", "orders", ADS_BEFORE_INSERT,
                     "RAISE boom(777, 'no inserts');");
    CHECK(f.run("INSERT INTO orders(ID) VALUES (7)") != 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM orders") == "0");
}

TEST_CASE("trigger: failing AFTER returns error but the write persists (Q6)") {
    SpFixture f("openads_trg_after");
    f.create_trigger("t1", "orders", ADS_AFTER_INSERT,
                     "RAISE boom(778, 'after fails');");
    CHECK(f.run("INSERT INTO orders(ID) VALUES (7)") != 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM orders") == "1");
}

TEST_CASE("trigger: EXECUTE PROCEDURE chain into a script proc works") {
    SpFixture f("openads_trg_chain");
    f.create_proc("sp_note", "INSERT INTO audit(ID) VALUES (99);", "", "");
    f.create_trigger("t1", "orders", ADS_AFTER_INSERT,
                     "EXECUTE PROCEDURE sp_note();");
    CHECK(f.run("INSERT INTO orders(ID) VALUES (7)") == 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM audit") == "1");
    CHECK(f.scalar("SELECT ID FROM audit") == "99");
}

TEST_CASE("trigger: SQL UPDATE failing AFTER trigger propagates (Q6)") {
    SpFixture f("openads_trg_upd");
    // seed a row (no trigger yet), then attach a failing AFTER UPDATE.
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (5)") == 0);
    f.create_trigger("tu", "orders", ADS_AFTER_UPDATE,
                     "RAISE boom(556, 'update fails');");
    CHECK(f.run("UPDATE orders SET ID = 6 WHERE ID = 5") != 0);
    CHECK(f.scalar("SELECT ID FROM orders") == "6");   // AFTER: write persists
}

TEST_CASE("trigger: UPDATE fires trigger whose EXECUTE PROCEDURE fails (chain)") {
    // Mirrors the pmsys "Update AuditLog" trigger -> failing proc chain: the
    // proc's error must propagate out through the trigger to fail the UPDATE.
    SpFixture f("openads_trg_procfail");
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (5)") == 0);
    f.create_proc("sp_bad", "RAISE inner_err(559, 'proc blew up');", "", "");
    f.create_trigger("tp", "orders", ADS_AFTER_UPDATE,
                     "EXECUTE PROCEDURE sp_bad();");
    UNSIGNED32 rc = f.run("UPDATE orders SET ID = 6 WHERE ID = 5");
    INFO("chain error: " << f.last_msg);
    CHECK(rc != 0);
}

TEST_CASE("trigger: SQL DELETE failing BEFORE trigger blocks the delete (Q6)") {
    SpFixture f("openads_trg_del");
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (5)") == 0);
    f.create_trigger("td", "orders", ADS_BEFORE_DELETE,
                     "RAISE boom(557, 'no deletes');");
    CHECK(f.run("DELETE FROM orders WHERE ID = 5") != 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM orders") == "1");  // still there
}

// ---- S3: cursors (docs/script-engine.md §11) ------------------------------

TEST_CASE("S3 proc: WHILE FETCH cursor copies rows table-to-table (C21c)") {
    SpFixture f("openads_s3_curcopy");
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (1)") == 0);
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (2)") == 0);
    REQUIRE(f.run("INSERT INTO orders(ID) VALUES (3)") == 0);
    f.create_proc("sp_copy",
                  "DECLARE c CURSOR AS SELECT * FROM orders; "
                  "OPEN c; "
                  "WHILE FETCH c DO "
                  "  INSERT INTO audit(ID) VALUES (c.ID); "
                  "END WHILE; "
                  "CLOSE c;",
                  "", "");
    UNSIGNED32 rc = f.run("EXECUTE PROCEDURE sp_copy()");
    INFO("sp_copy error: " << f.last_msg);
    CHECK(rc == 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM audit") == "3");
}

TEST_CASE("S3 proc: cursor over EXECUTE PROCEDURE result (C23)") {
    SpFixture f("openads_s3_curproc");
    f.create_proc("sp_rows",
                  "INSERT INTO __output(v) VALUES (10); "
                  "INSERT INTO __output(v) VALUES (32);",
                  "", "v,INTEGER;");
    f.create_proc("sp_sum",
                  "DECLARE c CURSOR AS EXECUTE PROCEDURE sp_rows(); "
                  "DECLARE @n INTEGER; "
                  "@n = 0; OPEN c; "
                  "WHILE FETCH c DO @n = @n + c.v; END WHILE; CLOSE c; "
                  "INSERT INTO __output(total) VALUES (@n);",
                  "", "total,INTEGER;");
    CHECK(f.scalar("EXECUTE PROCEDURE sp_sum()") == "42");
}

TEST_CASE("S3 proc: FETCH boolean + CATCH specific + FINALLY (F-probes)") {
    SpFixture f("openads_s3_tryfin");
    f.create_proc("sp_fin",
                  "DECLARE @s CHAR(20); "
                  "@s = 'x'; "
                  "TRY RAISE boom(9,'b'); "
                  "CATCH boom @s = TRIM(@s) + 'c'; "
                  "FINALLY @s = TRIM(@s) + 'f'; "
                  "END TRY; "
                  "INSERT INTO __output(r) VALUES (@s);",
                  "", "r,CHAR,20;");
    CHECK(f.scalar("EXECUTE PROCEDURE sp_fin()") == "xcf");
}

TEST_CASE("S3 trigger: cursor over __new row image, typed fields (Trig_Container shape)") {
    SpFixture f("openads_s3_trigcur");
    // n.ID is numeric (N field) — `n.ID > 1` requires the typed row image.
    f.create_trigger("t1", "orders", ADS_AFTER_INSERT,
                     "DECLARE n CURSOR AS SELECT * FROM __new; "
                     "OPEN n; FETCH n; "
                     "IF n.ID > 1 THEN "
                     "  INSERT INTO audit(ID) VALUES (n.ID); "
                     "ENDIF; "
                     "CLOSE n;");
    UNSIGNED32 rc = f.run("INSERT INTO orders(ID) VALUES (1)");
    INFO("first insert: " << f.last_msg);
    CHECK(rc == 0);
    CHECK(f.run("INSERT INTO orders(ID) VALUES (7)") == 0);
    // Only the second insert (ID 7 > 1) audits.
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM audit") == "1");
    CHECK(f.scalar("SELECT ID FROM audit") == "7");
}

TEST_CASE("S3: declare-after-executable fails the statement (C28)") {
    SpFixture f("openads_s3_declorder");
    f.create_proc("sp_bad",
                  "DECLARE @a INTEGER; @a = 1; DECLARE @b INTEGER;",
                  "", "");
    CHECK(f.run("EXECUTE PROCEDURE sp_bad()") != 0);
}

TEST_CASE("trigger: script control flow runs in trigger bodies now") {
    // The old fragment skipped IF entirely; the body below writes only
    // when the condition holds.
    SpFixture f("openads_trg_if");
    f.create_trigger("t1", "orders", ADS_AFTER_INSERT,
                     "DECLARE @n INTEGER; "
                     "@n = (SELECT COUNT(*) FROM orders); "
                     "IF @n > 0 THEN INSERT INTO audit(ID) VALUES (@n); "
                     "ENDIF;");
    UNSIGNED32 rc = f.run("INSERT INTO orders(ID) VALUES (1)");
    INFO("trigger error: " << f.last_msg);
    CHECK(rc == 0);
    CHECK(f.scalar("SELECT COUNT(*) AS n FROM audit") == "1");
}
