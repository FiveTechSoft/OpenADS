// UPDATE / DELETE WHERE features used by ADS maintenance scripts (xoffice3
// Sanitize): EMPTY(), TRIM(), NOT IN, column-vs-column, ROWID dedupe via
// GROUP BY ... MIN(ROWID), correlated IN / EXISTS subqueries, UPDATE SET
// <col> = <othercol> / (<correlated scalar subquery>), SELECT ... INTO
// <table>, and AdsShowDeleted applying to connections opened after it.
#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct ShowDeletedRestore {
    ~ShowDeletedRestore() { AdsShowDeleted(1); }
};

struct Db {
    fs::path  dir;
    ADSHANDLE conn = 0;
    ADSHANDLE stmt = 0;

    explicit Db(const char* name) {
        dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        open();
    }
    void open() {
        std::string p = dir.string();
        REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(p.data()),
                             ADS_LOCAL_SERVER, nullptr, nullptr, 0,
                             &conn) == 0);
        REQUIRE(AdsCreateSQLStatement(conn, &stmt) == 0);
    }
    void close() {
        if (stmt) AdsCloseSQLStatement(stmt);
        if (conn) AdsDisconnect(conn);
        stmt = conn = 0;
    }
    ~Db() {
        close();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void create(const char* table, const char* fields) {
        ADSHANDLE h = 0;
        std::string t = table, f = fields;
        REQUIRE(AdsCreateTable(conn, reinterpret_cast<UNSIGNED8*>(t.data()),
                               nullptr, ADS_CDX, 0, 0, 0, 0,
                               reinterpret_cast<UNSIGNED8*>(f.data()),
                               &h) == 0);
        AdsCloseTable(h);
    }
    UNSIGNED32 exec(const std::string& sql) {
        std::vector<UNSIGNED8> b(sql.begin(), sql.end());
        b.push_back(0);
        ADSHANDLE cur = 0;
        UNSIGNED32 rc = AdsExecuteSQLDirect(stmt, b.data(), &cur);
        if (cur) AdsCloseTable(cur);
        return rc;
    }
    void run(const std::string& sql) {
        INFO(sql);
        REQUIRE(exec(sql) == 0);
    }
    // Values of `field` over the rows `sql` returns (right-trimmed).
    std::vector<std::string> col(const std::string& sql, const char* field) {
        INFO(sql);
        std::vector<UNSIGNED8> b(sql.begin(), sql.end());
        b.push_back(0);
        ADSHANDLE cur = 0;
        REQUIRE(AdsExecuteSQLDirect(stmt, b.data(), &cur) == 0);
        std::vector<std::string> out;
        REQUIRE(AdsGotoTop(cur) == 0);
        for (;;) {
            UNSIGNED16 eof = 0;
            AdsAtEOF(cur, &eof);
            if (eof) break;
            UNSIGNED8 buf[128] = {0};
            UNSIGNED32 len = sizeof(buf);
            REQUIRE(AdsGetField(cur, (UNSIGNED8*)field, buf, &len, 0) == 0);
            std::string s(reinterpret_cast<char*>(buf), len);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            out.push_back(s);
            AdsSkip(cur, 1);
        }
        AdsCloseTable(cur);
        return out;
    }
    int count(const std::string& table) {
        auto v = col("SELECT COUNT(*) AS n FROM " + table, "n");
        REQUIRE(v.size() == 1);
        return std::stoi(v[0]);
    }
};

using V = std::vector<std::string>;

}  // namespace

TEST_CASE("DML WHERE: EMPTY(), TRIM(), NOT IN list, column = column") {
    ShowDeletedRestore restore;
    AdsShowDeleted(0);
    Db db("openads_dml_where_basic");
    db.create("t.dbf", "ID,Character,6;NM,Character,6;N,Numeric,5");
    db.run("INSERT INTO t (ID, NM, N) VALUES ('A', 'x', 1)");
    db.run("INSERT INTO t (ID, NM, N) VALUES ('', 'y', 2)");
    db.run("INSERT INTO t (ID, NM, N) VALUES ('B', '', 0)");
    db.run("INSERT INTO t (ID, NM, N) VALUES ('  C', 'z', 3)");
    db.run("INSERT INTO t (ID, NM, N) VALUES ('D', 'D', 4)");

    // SELECT side: EMPTY() on a numeric is zero, TRIM() strips leading blanks.
    CHECK(db.col("SELECT ID FROM t WHERE EMPTY(N)", "ID") == V{"B"});
    CHECK(db.col("SELECT ID FROM t WHERE TRIM(ID) = 'C'", "ID") == V{"  C"});
    CHECK(db.col("SELECT ID FROM t WHERE NOT EMPTY(ID) AND NOT EMPTY(NM) "
                 "ORDER BY ID", "ID") == V{"  C", "A", "D"});

    db.run("DELETE FROM t WHERE EMPTY(ID) OR EMPTY(NM)");
    CHECK(db.count("t") == 3);
    db.run("DELETE FROM t WHERE ID = NM");               // same-row columns
    CHECK(db.col("SELECT ID FROM t ORDER BY ID", "ID") == V{"  C", "A"});
    db.run("DELETE FROM t WHERE ID NOT IN ('A')");
    CHECK(db.col("SELECT ID FROM t", "ID") == V{"A"});
    db.run("UPDATE t SET NM = ID WHERE TRIM(NM) <> ''");
    CHECK(db.col("SELECT NM FROM t", "NM") == V{"A"});
}

TEST_CASE("DELETE ... ROWID NOT IN (SELECT MIN(ROWID) ... GROUP BY) keeps the "
          "first row of each group") {
    ShowDeletedRestore restore;
    AdsShowDeleted(0);
    Db db("openads_dml_rowid_dedupe");
    db.create("d.dbf", "K,Character,4;J,Character,4;V,Character,4");
    const char* rows[][3] = {{"a", "x", "1"}, {"b", "x", "2"}, {"a", "x", "3"},
                             {"c", "y", "4"}, {"b", "y", "5"}, {"a", "y", "6"}};
    for (auto& r : rows)
        db.run(std::string("INSERT INTO d (K, J, V) VALUES ('") + r[0] +
               "', '" + r[1] + "', '" + r[2] + "')");
    db.run("DELETE FROM d WHERE rowid NOT IN "
           "(SELECT MIN(rowid) FROM d GROUP BY K, J)");
    CHECK(db.col("SELECT V FROM d ORDER BY V", "V") ==
          V{"1", "2", "4", "5", "6"});
    db.run("DELETE FROM d WHERE rowid NOT IN "
           "(SELECT MAX(rowid) FROM d GROUP BY K)");
    CHECK(db.col("SELECT V FROM d ORDER BY V", "V") == V{"4", "5", "6"});
}

TEST_CASE("correlated NOT IN / NOT EXISTS in DELETE, both equality orientations") {
    ShowDeletedRestore restore;
    AdsShowDeleted(0);
    Db db("openads_dml_correlated");
    db.create("p.dbf", "PID,Character,4;NM,Character,8");
    db.create("c.dbf", "PID,Character,4;NM,Character,8;ALT,Character,4");
    db.run("INSERT INTO p (PID, NM) VALUES ('1', 'one')");
    db.run("INSERT INTO p (PID, NM) VALUES ('2', 'two')");
    db.run("INSERT INTO p (PID, NM) VALUES ('3', 'three')");
    for (const char* k : {"1", "9", "2", "8", "3"})
        db.run(std::string("INSERT INTO c (PID) VALUES ('") + k + "')");

    db.run("DELETE FROM c WHERE PID NOT IN "
           "(SELECT PID FROM p WHERE p.PID = c.PID)");
    CHECK(db.col("SELECT PID FROM c ORDER BY PID", "PID") == V{"1", "2", "3"});

    db.run("INSERT INTO c (PID) VALUES ('7')");
    db.run("DELETE FROM c WHERE PID NOT IN "
           "(SELECT PID FROM p WHERE c.PID = p.PID)");   // outer on the left
    CHECK(db.col("SELECT PID FROM c ORDER BY PID", "PID") == V{"1", "2", "3"});

    db.run("INSERT INTO c (PID) VALUES ('6')");
    db.run("DELETE FROM c WHERE NOT EXISTS "
           "(SELECT PID FROM p WHERE p.PID = c.PID)");
    CHECK(db.col("SELECT PID FROM c ORDER BY PID", "PID") == V{"1", "2", "3"});

    // A residual inner filter alongside the correlation.
    db.run("DELETE FROM c WHERE PID IN "
           "(SELECT PID FROM p WHERE p.PID = c.PID AND NM = 'two')");
    CHECK(db.col("SELECT PID FROM c ORDER BY PID", "PID") == V{"1", "3"});
}

TEST_CASE("UPDATE SET <col> = <othercol>, <col> = (correlated scalar subquery)") {
    ShowDeletedRestore restore;
    AdsShowDeleted(0);
    Db db("openads_dml_update_sources");
    db.create("p.dbf", "PID,Character,4;NM,Character,8");
    db.create("c.dbf", "PID,Character,4;NM,Character,8;ALT,Character,4");
    db.run("INSERT INTO p (PID, NM) VALUES ('1', 'one')");
    db.run("INSERT INTO p (PID, NM) VALUES ('2', 'two')");
    for (const char* k : {"1", "2", "5"})
        db.run(std::string("INSERT INTO c (PID, NM, ALT) VALUES ('") + k +
               "', 'old', 'zz')");
    db.run("UPDATE c SET NM = (SELECT NM FROM p WHERE p.PID = c.PID), "
           "ALT = PID WHERE ALT <> PID");
    CHECK(db.col("SELECT NM FROM c ORDER BY PID", "NM") == V{"one", "two", ""});
    CHECK(db.col("SELECT ALT FROM c ORDER BY PID", "ALT") == V{"1", "2", "5"});
}

TEST_CASE("SELECT ... INTO <table> creates it; AdsShowDeleted reaches "
          "connections opened afterwards") {
    ShowDeletedRestore restore;
    AdsShowDeleted(1);
    Db db("openads_dml_select_into");
    db.create("s.dbf", "K,Character,4;V,Character,4");
    for (const char* k : {"a", "b", "c"})
        db.run(std::string("INSERT INTO s (K, V) VALUES ('") + k + "', '" + k +
               "')");
    db.run("DELETE FROM s WHERE K = 'b'");

    // SET DELETED ON before connecting (Sanitize connects per statement).
    db.close();
    AdsShowDeleted(0);
    db.open();
    CHECK(db.count("s") == 2);
    db.run("SELECT K, V INTO s2 FROM s");
    CHECK(fs::exists(db.dir / "s2.dbf"));
    CHECK(db.col("SELECT K FROM s2 ORDER BY K", "K") == V{"a", "c"});
    // A failed SELECT INTO creates nothing.
    CHECK(db.exec("SELECT NOPE INTO s3 FROM s") != 0);
    CHECK_FALSE(fs::exists(db.dir / "s3.dbf"));

    // SET DELETED OFF: ADS SQL sees deleted DBF rows (and copies them).
    db.close();
    AdsShowDeleted(1);
    db.open();
    CHECK(db.count("s") == 3);
    db.run("SELECT K INTO s4 FROM s");
    CHECK(db.count("s4") == 3);
}
