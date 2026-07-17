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
    CHECK(run_ret("DECLARE @r CHAR(10); IF 2 > 1 THEN @r = 'yes'; "
                  "ELSE @r = 'no'; ENDIF; RETURN @r;").s == "yes");
    CHECK(run_ret("DECLARE @r CHAR(10); IF 1 > 2 THEN @r = 'a'; "
                  "ELSEIF 3 > 2 THEN @r = 'b'; ELSE @r = 'c'; ENDIF; "
                  "RETURN @r;").s == "b");
    CHECK(run_ret("DECLARE @r CHAR(10); IF 2 > 1 THEN @r = 'yes'; END IF; "
                  "RETURN @r;").s == "yes");
    // ELSE IF (two words, nested) — P6b
    CHECK(run_ret("DECLARE @r CHAR(10); IF 1 > 2 THEN @r = 'a'; "
                  "ELSE IF 3 > 2 THEN @r = 'b'; ENDIF; ENDIF; "
                  "RETURN @r;").s == "b");
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
    CHECK(run_ret("DECLARE @r CHAR(10); "
                  "TRY @r = CONVERT('abc', SQL_INTEGER); "
                  "CATCH ALL @r = 'caught'; END TRY; RETURN @r;")
              .s == "caught");
    CHECK(run_ret("DECLARE @r CHAR(200); TRY RAISE myerror(1234, 'boom'); "
                  "CATCH ALL @r = __errtext; END TRY; RETURN @r;")
              .s == "boom");
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
    auto v = run_ret(
        "DECLARE month INTEGER; DECLARE year INTEGER;\r\n"
        "month = 2; year = 2026;\r\n"
        "DECLARE @date1 TIMESTAMP;\r\n"
        "DECLARE @date2 TIMESTAMP;\r\n"
        "DECLARE @s STRING;\r\n"
        "DECLARE @dtstring STRING;\r\n"
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
