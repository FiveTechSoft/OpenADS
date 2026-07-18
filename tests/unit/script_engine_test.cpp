// OpenADS script engine — S1 unit tests (docs/script-engine.md §6).
//
// RCB 07/17/2026: each case mirrors an oracle probe (Pnn) run against SAP
// ADS 11 on 2026-07-17 — expected values here ARE SAP's answers, so these
// tests double as the differential battery for the pure-language subset.
#include "doctest.h"
#include "engine/script/exec.h"
#include "engine/script/parser.h"

#include <memory>
#include <string>

using namespace openads::script;

namespace {

// Run a script (no bridge) and return its RETURN value.
Value run_ret(const std::string& src) {
    auto prog = compile(src);
    REQUIRE(prog.has_value());
    Executor ex(nullptr);
    auto r = ex.run(*prog.value());
    REQUIRE(r.has_value());
    REQUIRE(r.value().returned);
    return r.value().return_value;
}

// Run and expect a script/parse error.
bool fails(const std::string& src) {
    auto prog = compile(src);
    if (!prog.has_value()) return true;
    Executor ex(nullptr);
    auto r = ex.run(*prog.value());
    return !r.has_value();
}

// RCB 07/18/2026 (S3): CHAR(N) variables are space-padded to N (probes
// N2-N5), so RETURNed CHAR values compare via their rtrimmed text — the
// same view SAP's client display gives.
std::string rtrim(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
std::string run_rets(const std::string& src) { return rtrim(run_ret(src).s); }

}  // namespace

TEST_CASE("script: assignment, arithmetic, RETURN (P1)") {
    auto v = run_ret("DECLARE @x INTEGER; @x = 5; RETURN @x;");
    CHECK(v.type == Type::Integer);
    CHECK(v.i == 5);
}

TEST_CASE("script: plain (non-@) variable names, case-insensitive (P2/P3)") {
    auto v = run_ret("DECLARE y INTEGER; Y = 7; RETURN y;");
    CHECK(v.i == 7);
    v = run_ret("DECLARE @Xx INTEGER; @xX = 9; RETURN @XX;");
    CHECK(v.i == 9);
}

TEST_CASE("script: DECLAREd variable starts NULL (P4)") {
    auto v = run_ret("DECLARE @n INTEGER; RETURN IIF(@n IS NULL, 1, 0);");
    CHECK(v.i == 1);
}

TEST_CASE("script: IF / ELSEIF / ELSE / ENDIF and END IF (P5-P7)") {
    CHECK(run_rets("DECLARE @r CHAR(10); IF 2 > 1 THEN @r = 'yes'; "
                   "ELSE @r = 'no'; ENDIF; RETURN @r;") == "yes");
    CHECK(run_rets("DECLARE @r CHAR(10); IF 1 > 2 THEN @r = 'a'; "
                   "ELSEIF 3 > 2 THEN @r = 'b'; ELSE @r = 'c'; ENDIF; "
                   "RETURN @r;") == "b");
    CHECK(run_rets("DECLARE @r CHAR(10); IF 2 > 1 THEN @r = 'yes'; END IF; "
                   "RETURN @r;") == "yes");
    // ELSE IF (two words, nested) — P6b
    CHECK(run_rets("DECLARE @r CHAR(10); IF 1 > 2 THEN @r = 'a'; "
                   "ELSE IF 3 > 2 THEN @r = 'b'; ENDIF; ENDIF; "
                   "RETURN @r;") == "b");
}

TEST_CASE("script: WHILE / LEAVE / CONTINUE; BREAK is an error (P8-P11)") {
    CHECK(run_ret("DECLARE @i INTEGER; DECLARE @s INTEGER; @i = 0; @s = 0; "
                  "WHILE @i < 5 DO @s = @s + @i; @i = @i + 1; END WHILE; "
                  "RETURN @s;").i == 10);
    CHECK(run_ret("DECLARE @i INTEGER; @i = 0; WHILE @i < 10 DO "
                  "@i = @i + 1; IF @i = 3 THEN LEAVE; ENDIF; END WHILE; "
                  "RETURN @i;").i == 3);
    CHECK(run_ret("DECLARE @i INTEGER; DECLARE @s INTEGER; @i = 0; @s = 0; "
                  "WHILE @i < 5 DO @i = @i + 1; "
                  "IF @i = 2 THEN CONTINUE; ENDIF; @s = @s + @i; "
                  "END WHILE; RETURN @s;").i == 13);
    // BREAK is not ADS (P9): it's an unknown statement → falls to embedded
    // SQL, which has no bridge here → runtime error either way.
    CHECK(fails("DECLARE @i INTEGER; @i = 0; WHILE @i < 10 DO "
                "@i = @i + 1; IF @i = 3 THEN BREAK; ENDIF; END WHILE; "
                "RETURN @i;"));
}

TEST_CASE("script: strict typing (P13/P19/P31/P33)") {
    CHECK(fails("RETURN '5' + 1;"));                       // P13
    CHECK(fails("DECLARE @i INTEGER; @i = 'abc'; RETURN @i;"));   // P19
    CHECK(fails("DECLARE @c CHAR(10); @c = 5; RETURN @c;"));      // P31
    // CONVERT bridges the gap (P13b)
    CHECK(run_ret("RETURN CONVERT('5', SQL_INTEGER) + 1;").i == 6);
}

TEST_CASE("script: numeric semantics (P14/P29/P32)") {
    CHECK(run_ret("RETURN 7 / 2;").i == 3);                // integer division
    auto v = run_ret("RETURN 7.0 / 2;");
    CHECK(v.type == Type::Double);
    CHECK(v.d == doctest::Approx(3.5));
    CHECK(run_ret("RETURN 7 % 3;").i == 1);
    CHECK(run_ret("RETURN MOD(7, 3);").i == 1);
    CHECK(run_ret("RETURN 2 + 3 * 4;").i == 14);           // precedence
    CHECK(run_ret("RETURN -5 + 2;").i == -3);
    CHECK(run_ret("RETURN 2 * -3;").i == -6);
    CHECK(run_ret("DECLARE @i INTEGER; @i = 3.7; RETURN @i;").i == 3); // P32
}

TEST_CASE("script: CHAR(N) truncation on assign (P26)") {
    auto v = run_ret("DECLARE @c CHAR(3); @c = 'abcdef'; RETURN @c;");
    CHECK(v.s == "abc");
}

TEST_CASE("script: comparisons — trailing spaces, only = and <> (P27/P28)") {
    CHECK(run_ret("RETURN IIF('a' = 'a  ', 1, 0);").i == 1);
    CHECK(run_ret("RETURN IIF('a' < 'b', 1, 0);").i == 1);
    CHECK(run_ret("RETURN IIF(1 <> 2, 1, 0);").i == 1);
    CHECK(fails("RETURN IIF(1 != 2, 1, 0);"));
    CHECK(fails("RETURN IIF(1 == 1, 1, 0);"));
}

TEST_CASE("script: three-valued logic and NULL (P12/P34)") {
    // NULL propagates through arithmetic
    CHECK(run_ret("DECLARE @n INTEGER; "
                  "RETURN IIF(@n + 1 IS NULL, 1, 0);").i == 1);
    // NULL = NULL is not true (P12c)
    CHECK(run_ret("DECLARE @n INTEGER; "
                  "RETURN IIF(@n = @n, 1, 0);").i == 0);
    // unknown OR TRUE = true; unknown AND TRUE = not-true (P34)
    CHECK(run_ret("DECLARE @n INTEGER; "
                  "RETURN IIF(@n > 0 OR TRUE, 1, 0);").i == 1);
    CHECK(run_ret("DECLARE @n INTEGER; "
                  "RETURN IIF(@n > 0 AND TRUE, 1, 0);").i == 0);
}

TEST_CASE("script: logical ops on TRUE/FALSE (P20)") {
    CHECK(run_ret("DECLARE @b LOGICAL; @b = 3 > 2 AND NOT (1 = 2); "
                  "RETURN IIF(@b, 1, 0);").i == 1);
}

TEST_CASE("script: CASE simple and searched (P15/P16)") {
    auto v = run_ret(
        "DECLARE @p CHAR(2); DECLARE @f CHAR(10); @f = 'leaseid'; "
        "@p = CASE @f WHEN 'ptkey' THEN 'PT' WHEN 'leaseid' THEN 'LS' END; "
        "RETURN @p;");
    CHECK(v.s == "LS");
    CHECK(run_ret("RETURN CASE WHEN 3 > 2 THEN 'big' ELSE 'small' END;")
              .s == "big");
}

TEST_CASE("script: dates — {d} literal, DATE+int, DATE-DATE (P17e)") {
    auto v = run_ret("DECLARE @d DATE; @d = {d '2026-02-28'}; "
                     "RETURN @d + 1;");
    CHECK(v.type == Type::Date);
    CHECK(to_display(v) == "03/01/2026");
    CHECK(run_ret("DECLARE @d DATE; @d = {d '2026-02-28'}; "
                  "RETURN @d - {d '2026-02-01'};").i == 27);
}

TEST_CASE("script: CREATETIMESTAMP + CAST AS SQL_DATE (P17f, EoM shape)") {
    auto v = run_ret(
        "RETURN CAST(CREATETIMESTAMP(2026, 2, 28, 0, 0, 0, 0) AS SQL_DATE);");
    CHECK(v.type == Type::Date);
    CHECK(to_display(v) == "02/28/2026");
    auto ts = run_ret("RETURN CREATETIMESTAMP(2026, 2, 28, 23, 59, 59, 9);");
    CHECK(to_display(ts) == "02/28/2026 11:59:59 PM");
}

TEST_CASE("script: TRY/CATCH ALL + RAISE + __errcode/__errtext (P22/P23/P30)") {
    CHECK(run_rets("DECLARE @r CHAR(10); "
                   "TRY @r = CONVERT('abc', SQL_INTEGER); "
                   "CATCH ALL @r = 'caught'; END TRY; RETURN @r;")
              == "caught");
    CHECK(run_rets("DECLARE @r CHAR(200); TRY RAISE myerror(1234, 'boom'); "
                   "CATCH ALL @r = __errtext; END TRY; RETURN @r;")
              == "boom");
    CHECK(run_ret("DECLARE @c INTEGER; TRY RAISE e(1234, 'x'); "
                  "CATCH ALL @c = __errcode; END TRY; RETURN @c;")
              .i == 1234);
    // Uncaught RAISE propagates as an error.
    CHECK(fails("RAISE e(99, 'up');"));
}

TEST_CASE("script: string builtins (P25)") {
    CHECK(run_ret("RETURN SUBSTRING('hello', 2, 3);").s == "ell");
    CHECK(run_ret("RETURN POSITION('ll' IN 'hello');").i == 3);
    CHECK(run_ret("RETURN UPPER('ab');").s == "AB");
    CHECK(run_ret("RETURN LENGTH(TRIM('  x  '));").i == 1);
    CHECK(run_ret("RETURN REPEAT('0', 3);").s == "000");
    CHECK(run_ret("RETURN RIGHT('abcdef', 2);").s == "ef");
}

TEST_CASE("script: comments are ignored (-- // /* */)") {
    auto v = run_ret("-- line comment\n"
                     "// slash comment\n"
                     "/* block\n comment */\n"
                     "DECLARE @x INTEGER; @x = 1; RETURN @x;");
    CHECK(v.i == 1);
}

TEST_CASE("script: PMSYS-shaped body — NewSeqKey prefix CASE") {
    // The pure-language subset of newseqkey (no embedded SQL).
    auto v = run_ret(
        "DECLARE @s STRING;\r\n"
        "DECLARE @cPrefix CHAR( 2 ) ;\r\n"
        "DECLARE cField CHAR( 20 );\r\n"
        "cField = 'leaseid';\r\n"
        "cField = Trim( Lower( cField ) ) ;\r\n"
        "@cPrefix = CASE cField \r\n"
        "  WHEN 'ptkey' THEN 'PT' \r\n"
        "  WHEN 'leaseid' THEN 'LS' \r\n"
        "END ; \r\n"
        "@s = @cPrefix + '26';\r\n"
        "RETURN @s;");
    CHECK(v.s == "LS26");
}

TEST_CASE("script: unknown identifier and undeclared assignment are errors") {
    CHECK(fails("RETURN nosuchvar;"));
    CHECK(fails("@x = 1; RETURN @x;"));
    CHECK(fails("RETURN NoSuchFn(1);"));
}

TEST_CASE("script: embedded SQL without a bridge is a clear error") {
    CHECK(fails("INSERT INTO t VALUES (1);"));
    CHECK(fails("DECLARE @x INTEGER; @x = (SELECT 1 FROM t); RETURN @x;"));
}

TEST_CASE("script: to_sql_literal renders substitution literals") {
    CHECK(to_sql_literal(Value::character("o'brien")) == "'o''brien'");
    CHECK(to_sql_literal(Value::integer(42)) == "42");
    CHECK(to_sql_literal(Value::null()) == "NULL");
    CHECK(to_sql_literal(Value::logical(true)) == "TRUE");
    CHECK(to_sql_literal(Value::date(jdn_from_ymd(2026, 2, 28))) ==
          "{d '2026-02-28'}");
}

TEST_CASE("script: TIMESTAMPADD/TIMESTAMPDIFF/IFNULL + CONVERT text dates") {
    // DaysInMonth shape: string -> SQL_TIMESTAMP, month add, day diff.
    CHECK(run_ret("DECLARE @d1 TIMESTAMP; DECLARE @d2 TIMESTAMP; "
                  "@d1 = CONVERT('2026-02-01 00:00:00', SQL_TIMESTAMP); "
                  "@d2 = TimeStampAdd(SQL_TSI_MONTH, 1, @d1); "
                  "RETURN TimeStampDiff(SQL_TSI_DAY, @d1, @d2);").i == 28);
    // Month boundary counting (pmsys MonthsOnTheMarket: 2020-01 -> 2024-06).
    CHECK(run_ret("RETURN TIMESTAMPDIFF(SQL_TSI_MONTH, {d '2020-01-01'}, "
                  "{d '2024-06-01'});").i == 53);
    // Day clamp: Jan 31 + 1 month = Feb 28.
    CHECK(to_display(run_ret("RETURN TIMESTAMPADD(SQL_TSI_MONTH, 1, "
                             "{d '2026-01-31'});")) == "02/28/2026");
    CHECK(run_ret("DECLARE @n INTEGER; RETURN IFNULL(@n, 7);").i == 7);
    CHECK(run_ret("RETURN IFNULL(3, 7);").i == 3);
}

TEST_CASE("script: bare END terminates IF (MonthsOnTheMarket shape)") {
    CHECK(run_ret("DECLARE @r CHAR(3); IF 2 > 1 THEN @r = 'yes'; END; "
                  "RETURN @r;").s == "yes");
    // Bare END inside a WHILE still leaves END WHILE for the loop.
    // Trace: 0->1; 1->(IF hit)->2->3; 3<3 false -> exit with 3.
    CHECK(run_ret("DECLARE @i INTEGER; @i = 0; WHILE @i < 3 DO "
                  "IF @i = 1 THEN @i = @i + 1; END; @i = @i + 1; "
                  "END WHILE; RETURN @i;").i == 3);
}

TEST_CASE("script: PMSYS DaysInMonth body verbatim executes (SAP=28)") {
    // The real pmsys body, with params bound as SAP would.
    // Harness bindings must respect the declare-first rule (C28): all
    // DECLAREs precede the first executable statement, as SAP requires.
    auto v = run_ret(
        "DECLARE month INTEGER; DECLARE year INTEGER;\r\n"
        "DECLARE @date1 TIMESTAMP;\r\n"
        "DECLARE @date2 TIMESTAMP;\r\n"
        "DECLARE @s STRING;\r\n"
        "DECLARE @dtstring STRING;\r\n"
        "month = 2; year = 2026;\r\n"
        "@dtstring = '';\r\n"
        "//year 4 digits!\r\n"
        "@s = trim( CONVERT( year, SQL_CHAR ) );\r\n"
        "@dtstring = REPEAT( '0', 4-length( @s ) ) + @s ;\r\n"
        "@s = TRIM( CONVERT( month, SQL_CHAR ) );\r\n"
        "@dtstring = @dtstring + '-' + REPEAT( '0', 2 - LENGTH( @s ) ) + @s;\r\n"
        "@dtstring = @dtstring + '-01 00:00:00' ;\r\n"
        "@date1 = convert( @dtstring, SQL_TIMESTAMP );\r\n"
        "@date2 = TimeStampAdd( SQL_TSI_MONTH, 1 , @date1 ) ;\r\n"
        "RETURN ( TimeStampDiff( SQL_TSI_DAY, @date1, @date2 ) ) ;");
    CHECK(v.i == 28);
}

// ===========================================================================
// S3 — cursors, FINALLY/CATCH-specific, CHAR padding. Each case mirrors an
// oracle probe from docs/script-engine.md §11 (C/F/N series, 2026-07-18).
// ===========================================================================

namespace {

// A 3-row fake table cursor: id INTEGER (1,2,3), name CHAR ("aa","bb","cc").
struct FakeRowsCursor final : SqlCursor {
    int row = 0;                       // 0 = before first
    int nrows;
    explicit FakeRowsCursor(int n = 3) : nrows(n) {}
    bool next() override {
        if (row >= nrows) return false;
        ++row;
        return true;
    }
    std::size_t field_count() const override { return 2; }
    std::string field_name(std::size_t idx) const override {
        return idx == 0 ? "id" : "name";
    }
    Value field(std::size_t idx) override {
        if (idx == 0) return Value::integer(row);
        const char* names[] = {"aa", "bb", "cc"};
        return Value::character(row >= 1 && row <= 3 ? names[row - 1] : "");
    }
};

// Bridge answering "SELECT * FROM t1"-ish statements with FakeRowsCursor
// and recording every statement it executes (to assert substitution).
struct FakeBridge final : SqlBridge {
    std::vector<std::string> executed;
    int rows = 3;
    openads::util::Result<std::unique_ptr<SqlCursor>>
    exec(const std::string& sql) override {
        executed.push_back(sql);
        return std::unique_ptr<SqlCursor>(new FakeRowsCursor(rows));
    }
    bool has_udf(const std::string&) override { return false; }
    openads::util::Result<Value>
    call_udf(const std::string&, const std::vector<Value>&) override {
        return openads::util::Error{7200, 0, "no udfs", ""};
    }
};

// Run with a bridge; return the RETURN value.
Value run_br(SqlBridge* br, const std::string& src) {
    auto prog = compile(src);
    REQUIRE(prog.has_value());
    Executor ex(br);
    auto r = ex.run(*prog.value());
    REQUIRE(r.has_value());
    REQUIRE(r.value().returned);
    return r.value().return_value;
}

// Run with a bridge; return the error message ("" = no error).
std::string run_err(SqlBridge* br, const std::string& src) {
    auto prog = compile(src);
    if (!prog.has_value()) return prog.error().message;
    Executor ex(br);
    auto r = ex.run(*prog.value());
    return r.has_value() ? "" : r.error().message;
}

}  // namespace

TEST_CASE("script S3: cursor open/fetch/field/close lifecycle (C1)") {
    FakeBridge br;
    auto v = run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; "
        "RETURN c.name;");
    CHECK(rtrim(v.s) == "aa");
}

TEST_CASE("script S3: WHILE FETCH sums rows; END and END WHILE (C2/C3)") {
    FakeBridge br;
    CHECK(run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; "
        "@n = 0; OPEN c; WHILE FETCH c DO @n = @n + c.id; END WHILE; "
        "CLOSE c; RETURN @n;").i == 6);
    FakeBridge br2;
    CHECK(run_br(&br2,
        "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; "
        "@n = 0; OPEN c; WHILE FETCH c DO @n = @n + c.id; END; "
        "CLOSE c; RETURN @n;").i == 6);
}

TEST_CASE("script S3: field access before first FETCH / at EOF errors (C6/C7/C31)") {
    FakeBridge br;
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; RETURN c.id;")
          .find("before first row or after last row") != std::string::npos);
    FakeBridge br2;
    CHECK(run_err(&br2,
        "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; "
        "FETCH c; FETCH c; FETCH c; FETCH c; RETURN c.id;")
          .find("before first row or after last row") != std::string::npos);
}

TEST_CASE("script S3: closed-cursor / double-open / undeclared errors (C8-C10/C13/C14/C29)") {
    FakeBridge br;
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; RETURN c.id;")
          .find("closed cursor") != std::string::npos);
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; OPEN c; RETURN 1;")
          .find("already opened") != std::string::npos);
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; CLOSE c; RETURN 1;")
          .find("closed cursor") != std::string::npos);
    CHECK(run_err(&br,
        "DECLARE c CURSOR; OPEN c; RETURN 1;")
          .find("not defined") != std::string::npos);
    CHECK(run_err(&br, "OPEN c AS SELECT * FROM t1; RETURN 1;")
          .find("not found") != std::string::npos);
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; FETCH c; RETURN 1;")
          .find("closed cursor") != std::string::npos);
}

TEST_CASE("script S3: reopen rescans; OPEN AS rebinds (C11/C12/C15)") {
    FakeBridge br;
    CHECK(run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; "
        "@n = 0; OPEN c; WHILE FETCH c DO @n = @n + 1; END WHILE; CLOSE c; "
        "OPEN c; WHILE FETCH c DO @n = @n + 1; END WHILE; CLOSE c; "
        "RETURN @n;").i == 6);
    // DECLARE without AS + OPEN AS binds there (C12); the AS statement text
    // reaches the bridge.
    FakeBridge br2;
    CHECK(rtrim(run_br(&br2,
        "DECLARE c CURSOR; OPEN c AS SELECT * FROM t1; FETCH c; "
        "RETURN c.name;").s) == "aa");
    REQUIRE(br2.executed.size() == 1);
    CHECK(br2.executed[0].find("SELECT * FROM t1") != std::string::npos);
}

TEST_CASE("script S3: FETCH as IF condition (C24/C25)") {
    FakeBridge br;
    CHECK(rtrim(run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @r CHAR(5); "
        "OPEN c; IF FETCH c THEN @r = 'yes'; ELSE @r = 'no'; ENDIF; "
        "RETURN @r;").s) == "yes");
    FakeBridge none;
    none.rows = 0;
    CHECK(rtrim(run_br(&none,
        "DECLARE c CURSOR AS SELECT * FROM t1 WHERE id = 99; "
        "DECLARE @r CHAR(5); OPEN c; "
        "IF FETCH c THEN @r = 'yes'; ELSE @r = 'no'; ENDIF; "
        "RETURN @r;").s) == "no");
}

TEST_CASE("script S3: LEAVE exits WHILE FETCH (C30)") {
    FakeBridge br;
    CHECK(run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; DECLARE @n INTEGER; "
        "@n = 0; OPEN c; WHILE FETCH c DO @n = @n + 1; "
        "IF @n = 2 THEN LEAVE; ENDIF; END WHILE; RETURN @n;").i == 2);
}

TEST_CASE("script S3: cursor fields substitute into embedded SQL (C21/C22)") {
    FakeBridge br;
    auto v = run_br(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; "
        "OPEN c; FETCH c; "
        "INSERT INTO t2 (id) VALUES (c.id); "
        "RETURN 1;");
    CHECK(v.i == 1);
    REQUIRE(br.executed.size() == 2);
    CHECK(br.executed[1] == "INSERT INTO t2 (id) VALUES (1)");
}

TEST_CASE("script S3: bracketed cursor field name (C16)") {
    struct BrCursor final : SqlCursor {
        bool done = false;
        bool next() override { if (done) return false; done = true; return true; }
        std::size_t field_count() const override { return 1; }
        std::string field_name(std::size_t) const override { return "my name"; }
        Value field(std::size_t) override { return Value::character("aa"); }
    };
    struct BrBridge final : SqlBridge {
        openads::util::Result<std::unique_ptr<SqlCursor>>
        exec(const std::string&) override {
            return std::unique_ptr<SqlCursor>(new BrCursor);
        }
        bool has_udf(const std::string&) override { return false; }
        openads::util::Result<Value>
        call_udf(const std::string&, const std::vector<Value>&) override {
            return openads::util::Error{7200, 0, "no udfs", ""};
        }
    } br;
    CHECK(rtrim(run_br(&br,
        "DECLARE c CURSOR AS SELECT 1 FROM x; OPEN c; FETCH c; "
        "RETURN c.[my name];").s) == "aa");
}

TEST_CASE("script S3: cursor names case-insensitive; @-prefixed (C17/C18)") {
    FakeBridge br;
    CHECK(run_br(&br,
        "DECLARE AllCols CURSOR AS SELECT * FROM t1; OPEN allcols; "
        "FETCH ALLCOLS; RETURN allCols.ID;").i == 1);
    FakeBridge br2;
    CHECK(rtrim(run_br(&br2,
        "DECLARE @c CURSOR AS SELECT * FROM t1; OPEN @c; FETCH @c; "
        "RETURN @c.name;").s) == "aa");
}

TEST_CASE("script S3: unknown cursor field errors (C19)") {
    FakeBridge br;
    CHECK(run_err(&br,
        "DECLARE c CURSOR AS SELECT * FROM t1; OPEN c; FETCH c; "
        "RETURN c.nosuch;")
          .find("not found") != std::string::npos);
}

TEST_CASE("script S3: DECLARE after executable statement is an error (C28)") {
    CHECK(fails("DECLARE @a INTEGER; @a = 1; DECLARE @b INTEGER; RETURN 1;"));
}

TEST_CASE("script S3: TRY needs CATCH or FINALLY; FINALLY runs (F0-F2)") {
    CHECK(fails("DECLARE @s CHAR(10); TRY @s = 'a'; END TRY; RETURN @s;"));
    CHECK(run_rets(
        "DECLARE @s CHAR(10); @s = 'x'; "
        "TRY @s = TRIM(@s) + 'a'; FINALLY @s = TRIM(@s) + 'f'; END TRY; "
        "RETURN @s;") == "xaf");
    // Order on error: body → CATCH → FINALLY (F2b: "xcf").
    CHECK(run_rets(
        "DECLARE @s CHAR(10); @s = 'x'; "
        "TRY RAISE e(1,'b'); CATCH ALL @s = TRIM(@s) + 'c'; "
        "FINALLY @s = TRIM(@s) + 'f'; END TRY; RETURN @s;") == "xcf");
}

TEST_CASE("script S3: FINALLY runs on uncaught error, error propagates (F3)") {
    // Observable side effect through the bridge: the FINALLY INSERT runs
    // even though the RAISE is uncaught and fails the script.
    FakeBridge br;
    auto e = run_err(&br,
        "TRY RAISE e(1,'boom'); FINALLY INSERT INTO t3 (id) VALUES (9); "
        "END TRY;");
    CHECK(e.find("boom") != std::string::npos);
    REQUIRE(br.executed.size() == 1);
    CHECK(br.executed[0].find("INSERT INTO t3") != std::string::npos);
}

TEST_CASE("script S3: CATCH <specific> matches by RAISE name (F4-F6)") {
    CHECK(run_rets(
        "DECLARE @s CHAR(10); TRY RAISE myerr(5,'x'); "
        "CATCH myerr @s = 'specific'; END TRY; RETURN @s;") == "specific");
    // Case-insensitive (F6).
    CHECK(run_rets(
        "DECLARE @s CHAR(10); TRY RAISE MyErr(5,'x'); "
        "CATCH MYERR @s = 'caught'; END TRY; RETURN @s;") == "caught");
    // Non-matching name propagates (F5).
    CHECK(fails(
        "DECLARE @s CHAR(10); TRY RAISE other(5,'x'); "
        "CATCH myerr @s = 'wrong'; END TRY; RETURN @s;"));
}

TEST_CASE("script S3: CHAR(N) pads; LENGTH is rtrimmed length (N2-N5, F1d)") {
    // LENGTH of a padded CHAR(10) 'a' is 1 (N2); of '' is 0 (N3).
    CHECK(run_ret("DECLARE @s CHAR(10); @s = 'a'; RETURN LENGTH(@s);").i == 1);
    CHECK(run_ret("DECLARE @s CHAR(10); @s = ''; RETURN LENGTH(@s);").i == 0);
    // Concat uses the PADDED value: 'a' + 9 spaces + 'b' → LENGTH 11 (N4/N5).
    CHECK(run_ret(
        "DECLARE @s CHAR(10); DECLARE @t CHAR(30); @s = 'a'; "
        "@t = @s + 'b'; RETURN LENGTH(@t);").i == 11);
    CHECK(run_ret(
        "DECLARE @s CHAR(10); DECLARE @t CHAR(30); @s = 'a'; "
        "@t = @s + 'b'; RETURN @t;").s == "a         b                   ");
    // F1d: '' pads to 10 spaces; + 'a' truncates back to 10 spaces.
    CHECK(run_rets(
        "DECLARE @s CHAR(10); @s = ''; @s = @s + 'a'; RETURN @s;") == "");
}

TEST_CASE("script S3: nested cursor re-opened per outer row (C27)") {
    FakeBridge br;
    CHECK(run_br(&br,
        "DECLARE a CURSOR AS SELECT id FROM t1; DECLARE b CURSOR; "
        "DECLARE @n INTEGER; "
        "@n = 0; OPEN a; WHILE FETCH a DO "
        "OPEN b AS SELECT * FROM t1 WHERE id >= a.id; "
        "WHILE FETCH b DO @n = @n + 1; END WHILE; CLOSE b; "
        "END WHILE; CLOSE a; RETURN @n;").i == 9);   // 3 outer × 3 inner
    // Inner statement re-substituted per row: a.id = 1, 2, 3.
    REQUIRE(br.executed.size() == 4);
    CHECK(br.executed[1].find("id >= 1") != std::string::npos);
    CHECK(br.executed[2].find("id >= 2") != std::string::npos);
    CHECK(br.executed[3].find("id >= 3") != std::string::npos);
}
