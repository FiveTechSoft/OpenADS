#include "doctest.h"
#include "sql/parser.h"

#include <vector>
#include <functional>

using openads::sql::parse_select;
using openads::sql::WhereExpr;
using openads::sql::WhereOp;

namespace {

// Walk a left-skewed AND tree and flatten it to its Cmp leaves so the
// tests can assert against indexed positions (matching the old vector
// shape).
void collect_and_leaves(const WhereExpr* node,
                        std::vector<const openads::sql::WhereCmp*>& out) {
    if (node == nullptr) return;
    if (node->kind == WhereExpr::Kind::And) {
        for (auto& c : node->children) collect_and_leaves(c.get(), out);
    } else if (node->kind == WhereExpr::Kind::Cmp) {
        out.push_back(&node->cmp);
    }
}

}  // namespace

TEST_CASE("parse_select recognises SELECT * FROM <table>") {
    auto r = parse_select("SELECT * FROM clientes");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "clientes");
}

TEST_CASE("parse_select is case-insensitive on keywords") {
    auto r = parse_select("select * from VENTAS");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "VENTAS");
}

TEST_CASE("parse_select tolerates trailing semicolon and whitespace") {
    auto r = parse_select("   SELECT  *  FROM   data.dbf  ;  ");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data.dbf");
}

TEST_CASE("parse_select accepts projection lists (M10.8)") {
    auto r = parse_select("SELECT name, age FROM x");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "x");
    REQUIRE(r.value().projection.size() == 2);
    CHECK(r.value().projection[0] == "name");
    CHECK(r.value().projection[1] == "age");
}

TEST_CASE("parse_select reports missing FROM") {
    auto r = parse_select("SELECT *");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}

TEST_CASE("parse_select recognises a single-equality WHERE clause") {
    auto r = parse_select("SELECT * FROM data.dbf WHERE TAG = 'BAR'");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "data.dbf");
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column  == "TAG");
    CHECK(r.value().where->cmp.literal == "BAR");
}

TEST_CASE("parse_select WHERE accepts case-insensitive keyword and tight whitespace") {
    auto r = parse_select("select * from x where  Name='Anna'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column  == "Name");
    CHECK(r.value().where->cmp.literal == "Anna");
}

TEST_CASE("parse_select WHERE supports AND-joined comparisons") {
    auto r = parse_select(
        "SELECT * FROM x WHERE A = 'foo' AND B != 'bar' AND C >= 'z'");
    REQUIRE(r.has_value());
    std::vector<const openads::sql::WhereCmp*> leaves;
    collect_and_leaves(r.value().where.get(), leaves);
    REQUIRE(leaves.size() == 3);
    CHECK(leaves[0]->column == "A");
    CHECK(leaves[0]->op     == WhereOp::Eq);
    CHECK(leaves[1]->column == "B");
    CHECK(leaves[1]->op     == WhereOp::Ne);
    CHECK(leaves[2]->column == "C");
    CHECK(leaves[2]->op     == WhereOp::Ge);
}

TEST_CASE("parse_select WHERE accepts each comparison operator") {
    struct Case { const char* op; WhereOp expected; };
    Case cases[] = {
        {"=",  WhereOp::Eq},
        {"!=", WhereOp::Ne},
        {"<>", WhereOp::Ne},
        {"<",  WhereOp::Lt},
        {">",  WhereOp::Gt},
        {"<=", WhereOp::Le},
        {">=", WhereOp::Ge},
    };
    for (const auto& tc : cases) {
        std::string sql = std::string("SELECT * FROM x WHERE TAG ") + tc.op + " 'A'";
        auto r = parse_select(sql);
        REQUIRE(r.has_value());
        REQUIRE(r.value().where != nullptr);
        REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
        CHECK(r.value().where->cmp.op == tc.expected);
    }
}

TEST_CASE("parse_select WHERE rejects unterminated string literal") {
    auto r = parse_select("SELECT * FROM x WHERE TAG = 'oops");
    CHECK_FALSE(r.has_value());
    CHECK(r.error().code == 7200);
}

TEST_CASE("parse_select WHERE decodes doubled '' as an escaped quote") {
    auto r = parse_select("SELECT * FROM x WHERE NAME = 'O''Brien'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.literal == "O'Brien");
}

TEST_CASE("M10.3 parse_select WHERE supports OR + parens") {
    auto r = parse_select(
        "SELECT * FROM x WHERE (A = 'a' OR B = 'b') AND C = 'c'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::And);
    REQUIRE(r.value().where->children.size() == 2);
    CHECK(r.value().where->children[0]->kind == WhereExpr::Kind::Or);
    CHECK(r.value().where->children[1]->kind == WhereExpr::Kind::Cmp);
}

TEST_CASE("M10.3 parse_select WHERE supports NOT") {
    auto r = parse_select("SELECT * FROM x WHERE NOT TAG = 'a'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Not);
    CHECK(r.value().where->child->kind == WhereExpr::Kind::Cmp);
}

TEST_CASE("M10.3 parse_select WHERE accepts numeric literals") {
    auto r = parse_select("SELECT * FROM x WHERE AGE >= 18");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.is_numeric);
    CHECK(r.value().where->cmp.number == doctest::Approx(18));
    CHECK(r.value().where->cmp.op == WhereOp::Ge);
}

// --- ADS dialect compatibility (feat/sql-ads-dialect) ---------------------
// SAP ADS lets apps write SQL with constructs the strict-SQL parser used to
// reject. These three keep that legacy syntax parsing without forcing a
// rewrite. All three appear together in real ADS queries like:
//   SELECT {static} * FROM [articulo.dat] AS a WHERE ... ORDER BY a.col

TEST_CASE("ADS dialect: {static} cursor hint after SELECT is ignored") {
    auto r = parse_select("SELECT {static} * FROM articulo");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo");
    CHECK(r.value().projection.empty());   // `*` => no explicit projection
}

TEST_CASE("ADS dialect: {static} hint with a projection list") {
    auto r = parse_select("SELECT {static} name, age FROM x");
    REQUIRE(r.has_value());
    REQUIRE(r.value().projection.size() == 2);
    CHECK(r.value().projection[0] == "name");
    CHECK(r.value().projection[1] == "age");
}

TEST_CASE("ADS dialect: bracketed table/file name in FROM") {
    auto r = parse_select("SELECT * FROM [articulo.dat]");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo.dat");
}

TEST_CASE("ADS dialect: FROM <table> AS <alias>") {
    auto r = parse_select("SELECT * FROM articulo AS a WHERE a.cnombre <> 'N'");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo");
    CHECK(r.value().table_alias == "a");
    // The WHERE survives the alias and resolves the qualified column.
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "cnombre");
    CHECK(r.value().where->cmp.op == WhereOp::Ne);
}

TEST_CASE("ADS dialect: UPPER(col) on the WHERE left-hand side") {
    auto r = parse_select("SELECT * FROM x WHERE UPPER(name) <> 'N'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "name");
    CHECK(r.value().where->cmp.op == WhereOp::Ne);
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Upper);
    CHECK(r.value().where->cmp.literal == "N");
}

TEST_CASE("ADS dialect: LOWER(col) on the WHERE left-hand side") {
    auto r = parse_select("SELECT * FROM x WHERE LOWER(a.tag) = 'n'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "tag");   // alias dropped
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Lower);
    CHECK(r.value().where->cmp.op == WhereOp::Eq);
}

TEST_CASE("ADS dialect: bare column LHS keeps lhs_fn None") {
    auto r = parse_select("SELECT * FROM x WHERE tag = 'a'");
    REQUIRE(r.has_value());
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::None);
}

TEST_CASE("ADS dialect: UPPER(col) LIKE pattern in WHERE") {
    auto r = parse_select("SELECT * FROM x WHERE UPPER(name) LIKE 'A%'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->cmp.column == "name");
    CHECK(r.value().where->cmp.lhs_fn == openads::sql::WhereFn::Upper);
    CHECK(r.value().where->cmp.op == WhereOp::Like);
}

TEST_CASE("ADS dialect: constant predicate WHERE 1 = 1 folds to always-true") {
    auto r = parse_select("SELECT * FROM x WHERE 1 = 1");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    // Always-true is encoded as an AND node with no children.
    CHECK(r.value().where->kind == WhereExpr::Kind::And);
    CHECK(r.value().where->children.empty());
}

TEST_CASE("ADS dialect: constant predicate WHERE 1 = 2 folds to always-false") {
    auto r = parse_select("SELECT * FROM x WHERE 1 = 2");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    // Always-false is encoded as an OR node with no children.
    CHECK(r.value().where->kind == WhereExpr::Kind::Or);
    CHECK(r.value().where->children.empty());
}

TEST_CASE("ADS dialect: WHERE 1 = 1 AND <real predicate> keeps the predicate") {
    auto r = parse_select("SELECT * FROM x WHERE 1 = 1 AND name = 'a'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where->kind == WhereExpr::Kind::And);
    REQUIRE(r.value().where->children.size() == 2);
    CHECK(r.value().where->children[0]->kind == WhereExpr::Kind::And);  // folded 1=1
    CHECK(r.value().where->children[0]->children.empty());
    CHECK(r.value().where->children[1]->kind == WhereExpr::Kind::Cmp);
    CHECK(r.value().where->children[1]->cmp.column == "name");
}

TEST_CASE("ADS dialect: full legacy query (hint + bracket + alias)") {
    auto r = parse_select(
        "SELECT {static} * FROM [articulo.dat] AS a "
        "WHERE a.crefhabart <> 'N' AND a.cnombreart LIKE 'A%' "
        "ORDER BY a.cnombreart");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo.dat");
    CHECK(r.value().table_alias == "a");
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().order_by.has_value());
    CHECK(r.value().order_by.value().column == "cnombreart");
}

TEST_CASE("ADS dialect: complete legacy ERP search query parses") {
    auto r = parse_select(
        "SELECT {static} * FROM [articulo.dat] AS a "
        "WHERE 1 = 1 "
        "AND UPPER(a.creffacart) <> 'N' "
        "AND UPPER(a.crefhabart) <> 'N' "
        "AND UPPER(a.cnombreart) LIKE 'A%' "
        "ORDER BY a.cnombreart");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "articulo.dat");
    CHECK(r.value().table_alias == "a");
    REQUIRE(r.value().where != nullptr);
    REQUIRE(r.value().order_by.has_value());
    CHECK(r.value().order_by.value().column == "cnombreart");
}

// --- ADS dialect: comma-join (feat/sql-comma-join, #6) ---------------------
// SAP ADS apps write the classic SQL-89 comma-join `FROM a, b WHERE a.x = b.y`
// instead of `INNER JOIN ... ON`. The equality in the WHERE *is* the join
// predicate. We lower a two-table comma-join into the existing single
// inner_join AST: lift the one `col = col` equality into the JoinClause and
// blank that node so the rest of the WHERE stays as a row filter.

TEST_CASE("comma-join: FROM a, b WHERE a.x = b.y lowers to inner_join") {
    auto r = parse_select(
        "SELECT * FROM ord, cus WHERE ord.cust = cus.cust");
    REQUIRE(r.has_value());
    CHECK(r.value().table == "ord");
    REQUIRE(r.value().inner_join.has_value());
    CHECK(r.value().inner_join->table == "cus");
    CHECK(r.value().inner_join->left_column  == "cust");
    CHECK(r.value().inner_join->right_column == "cust");
    CHECK(r.value().inner_join->is_left  == false);
    CHECK(r.value().inner_join->is_right == false);
    CHECK(r.value().inner_join->is_full  == false);
}

TEST_CASE("comma-join: lifted join predicate leaves an always-true WHERE") {
    // Only the join key in the WHERE => after lifting, nothing is left to
    // filter on. An empty-AND node (always true) is the canonical encoding.
    auto r = parse_select(
        "SELECT * FROM ord, cus WHERE ord.cust = cus.cust");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where != nullptr);
    CHECK(r.value().where->kind == WhereExpr::Kind::And);
    CHECK(r.value().where->children.empty());   // always-true
}

TEST_CASE("comma-join: extra filters survive next to the join predicate") {
    auto r = parse_select(
        "SELECT * FROM ord o, cus c "
        "WHERE o.cust = c.cust AND c.name = 'Alice'");
    REQUIRE(r.has_value());
    REQUIRE(r.value().inner_join.has_value());
    CHECK(r.value().inner_join->table == "cus");
    // The residual filter `c.name = 'Alice'` must remain in the WHERE tree.
    REQUIRE(r.value().where != nullptr);
    bool found_name_filter = false;
    std::function<void(const WhereExpr*)> walk = [&](const WhereExpr* n) {
        if (n == nullptr) return;
        if (n->kind == WhereExpr::Kind::Cmp &&
            n->cmp.column == "name" && n->cmp.literal == "Alice") {
            found_name_filter = true;
        }
        for (auto& ch : n->children) walk(ch.get());
        walk(n->child.get());
    };
    walk(r.value().where.get());
    CHECK(found_name_filter);
}

TEST_CASE("comma-join: predicate write order is preserved for the executor") {
    // `FROM ord, cus WHERE cus.cust = ord.cust` — base table is `ord`, but
    // the predicate names the joined table first. The parser keeps the LHS
    // column as left_column; the executor resolves orientation against the
    // real schemas, so the parser just records what was written.
    auto r = parse_select(
        "SELECT * FROM ord, cus WHERE cus.code = ord.cust");
    REQUIRE(r.has_value());
    REQUIRE(r.value().inner_join.has_value());
    CHECK(r.value().inner_join->table == "cus");
    CHECK(r.value().inner_join->left_column  == "code");
    CHECK(r.value().inner_join->right_column == "cust");
}

TEST_CASE("comma-join: missing equi-join predicate is a cartesian error") {
    auto r = parse_select(
        "SELECT * FROM ord, cus WHERE ord.cust = 'C001'");
    CHECK_FALSE(r.has_value());   // cartesian products are not supported
}

TEST_CASE("comma-join: three+ comma tables parse for the N-way executor") {
    // ADS dialect — 3+ tables are now captured in from_tables and routed to
    // the N-way join executor (not the single inner_join lowering).
    auto r = parse_select(
        "SELECT * FROM a, b, c WHERE a.x = b.y AND b.z = c.w");
    REQUIRE(r.has_value());
    CHECK(r.value().from_tables.size() == 3);
    CHECK(r.value().from_tables[0].name == "a");
    CHECK(r.value().from_tables[1].name == "b");
    CHECK(r.value().from_tables[2].name == "c");
    CHECK_FALSE(r.value().inner_join.has_value());   // N-way path, not 2-table
}

TEST_CASE("comma-join: N-way with aliases, composite key and alias.* parse") {
    // A 5-table header/detail + dimensions report shape: a composite join
    // key (h<->d on two columns), qualified columns and a `<alias>.*`
    // wildcard. All must parse; the executor consumes from_tables +
    // select_items + the WHERE equalities.
    auto r = parse_select(
        "SELECT i.item_name, h.tx_date, d.* "
        "FROM doctype AS dt, doc AS h, line AS d, "
        "party AS p, item AS i "
        "WHERE ( i.item_id = d.item_id ) AND "
        "( h.party_id = p.party_id ) AND "
        "( h.doctype_id = d.doctype_id AND h.doc_no = d.doc_no ) AND "
        "( dt.doctype_id = h.doctype_id )");
    REQUIRE(r.has_value());
    CHECK(r.value().from_tables.size() == 5);
    CHECK(r.value().from_tables[1].alias == "h");
    CHECK_FALSE(r.value().inner_join.has_value());
    // projection: two plain qualified columns + one alias.* wildcard
    REQUIRE(r.value().select_items.size() == 3);
    CHECK(r.value().select_items[0].alias == "i");
    CHECK(r.value().select_items[0].column == "item_name");
    CHECK(r.value().select_items[1].alias == "h");
    CHECK(r.value().select_items[2].wildcard);
    CHECK(r.value().select_items[2].alias == "d");
}

TEST_CASE("WHERE: ODBC date escape {d 'YYYY-MM-DD'} parses to digits") {
    // ADS uses `{d '...'}` date constants. Reduced to YYYYMMDD so it
    // string-compares against the DBF Date field's raw bytes.
    auto r = parse_select(
        "SELECT * FROM doc WHERE tx_date >= {d '2026-01-01'}");
    REQUIRE(r.has_value());
    REQUIRE(r.value().where);
    CHECK(r.value().where->cmp.literal == "20260101");
}

TEST_CASE("comma-join: composite (multiple) join keys are rejected") {
    auto r = parse_select(
        "SELECT * FROM a, b WHERE a.x = b.y AND a.z = b.w");
    CHECK_FALSE(r.has_value());   // single-key join only
}

TEST_CASE("comma-join: mixing comma with explicit JOIN is rejected") {
    auto r = parse_select(
        "SELECT * FROM a, b JOIN c ON a.x = c.y WHERE a.p = b.q");
    CHECK_FALSE(r.has_value());
}
