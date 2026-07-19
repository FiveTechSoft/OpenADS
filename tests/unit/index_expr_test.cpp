#include "doctest.h"

#include "engine/index_expr.h"
#include "engine/table.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::Table;
using openads::engine::TableType;
using openads::engine::OpenMode;
using openads::engine::evaluate_index_expr;

namespace {

// Stage a tiny DBF with NAME C(10) + AGE N(3,0) + BORN D(8) so the
// expression evaluator has real fields to look up.
fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "expr.dbf";
    fs::remove(p);

    std::vector<std::uint8_t> buf;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        buf.insert(buf.end(), b, b + n);
    };
    auto pad = [&](std::size_t n) { for (std::size_t i = 0; i < n; ++i) buf.push_back(0); };

    // Header.
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 1;                       // 1 record
    hdr[8] = 32 + 32 * 3 + 1;        // header_len = 129
    hdr[10] = 1 + 10 + 3 + 8;         // record_len = 22
    push(hdr.data(), hdr.size());

    auto field = [&](const char* name, char type, std::uint8_t len) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), name, 11);
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = len;
        push(fd.data(), fd.size());
    };
    field("NAME", 'C', 10);
    field("AGE",  'N',  3);
    field("BORN", 'D',  8);
    buf.push_back(0x0D);

    // One record: " ALPHA      75 19990501"
    buf.push_back(' ');                                  // not deleted
    const char name[10] = {'A','L','P','H','A',' ',' ',' ',' ',' '};
    push(name, 10);
    push(" 75", 3);
    push("19990501", 8);
    buf.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(buf.data()),
        static_cast<std::streamsize>(buf.size()));
    (void)pad;
    return p;
}

Table open_table(const fs::path& p) {
    auto t = Table::open(p.string(), TableType::Cdx, OpenMode::Read);
    REQUIRE(t.has_value());
    Table tbl = std::move(t).value();
    REQUIRE(tbl.goto_top().has_value());
    return tbl;
}

} // namespace

TEST_CASE("index_expr: bare field name returns padded raw bytes") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr1";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto k = evaluate_index_expr(tbl, "NAME", 10);
    REQUIRE(k.has_value());
    CHECK(k.value() == "ALPHA     ");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: alias-qualified field name resolves to the field") {
    // Harbour `INDEX ON CUST->NAME` passes the key expression to the
    // RDD as the literal text "CUST->NAME". The `->` alias qualifier
    // must resolve to the field — an index built from it had every
    // key blank (recno-order index, failed SEEK).
    auto dir = fs::temp_directory_path() / "openads_idx_expr_alias";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto bare  = evaluate_index_expr(tbl, "NAME", 10);
    auto alias = evaluate_index_expr(tbl, "CUST->NAME", 10);
    REQUIRE(bare.has_value());
    REQUIRE(alias.has_value());
    CHECK(alias.value() == bare.value());

    // also nested inside a function call
    auto ubare  = evaluate_index_expr(tbl, "UPPER(NAME)", 10);
    auto ualias = evaluate_index_expr(tbl, "UPPER(CUST->NAME)", 10);
    REQUIRE(ubare.has_value());
    REQUIRE(ualias.has_value());
    CHECK(ualias.value() == ubare.value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: UPPER + LOWER on string fields") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr2";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto u = evaluate_index_expr(tbl, "UPPER(NAME)", 10);
    REQUIRE(u.has_value());
    CHECK(u.value() == "ALPHA     ");

    auto l = evaluate_index_expr(tbl, "LOWER(NAME)", 10);
    REQUIRE(l.has_value());
    CHECK(l.value() == "alpha     ");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: STR formats numerics with width and decimals") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr3";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto s = evaluate_index_expr(tbl, "STR(AGE, 4)", 4);
    REQUIRE(s.has_value());
    CHECK(s.value() == "  75");

    auto sd = evaluate_index_expr(tbl, "STR(AGE, 6, 1)", 6);
    REQUIRE(sd.has_value());
    CHECK(sd.value() == "  75.0");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: DTOS returns YYYYMMDD") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr4";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto d = evaluate_index_expr(tbl, "DTOS(BORN)", 8);
    REQUIRE(d.has_value());
    CHECK(d.value() == "19990501");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: concatenation with + builds compound keys") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr5";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto k = evaluate_index_expr(tbl, "RTRIM(NAME) + DTOS(BORN)", 14);
    REQUIRE(k.has_value());
    CHECK(k.value() == "ALPHA19990501 ");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: SUBSTR slices a string field") {
    auto dir = fs::temp_directory_path() / "openads_idx_expr6";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto k = evaluate_index_expr(tbl, "SUBSTR(NAME, 1, 3)", 3);
    REQUIRE(k.has_value());
    CHECK(k.value() == "ALP");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// ---- GitHub #131 regression: fixed-width operands, PADx, natural length ----

TEST_CASE("index_expr: field operands keep their declared width in compounds") {
    // #131 defect A — a char-field operand in a `+` compound must
    // contribute its FULL declared width (trailing blanks included),
    // matching the key Harbour computes for DbSeek. The old evaluator
    // used the rtrimmed decode, so a blank/short middle operand shrank
    // the stored key and every full-width seek missed.
    auto dir = fs::temp_directory_path() / "openads_idx_expr_fw";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    // NAME C(10) holds "ALPHA     " (5 + 5 blanks), BORN D(8).
    auto k = evaluate_index_expr(tbl, "NAME + DTOS(BORN)", 18);
    REQUIRE(k.has_value());
    CHECK(k.value() == "ALPHA     19990501");
    CHECK(k.value().size() == 18);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: key_len 0 returns the natural, unpadded key") {
    // The AdsCreateIndex61 composite-expression probe evaluates with
    // key_len=0 to size the tag. That must yield the natural fixed-width
    // key — resizing to 0 returned "" and pinned every composite tag to
    // the 32-byte fallback width (part of #131 defect A).
    auto dir = fs::temp_directory_path() / "openads_idx_expr_nat";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto bare = evaluate_index_expr(tbl, "NAME", 0);
    REQUIRE(bare.has_value());
    CHECK(bare.value() == "ALPHA     ");       // full C(10) width

    auto comp = evaluate_index_expr(tbl, "NAME + DTOS(BORN)", 0);
    REQUIRE(comp.has_value());
    CHECK(comp.value() == "ALPHA     19990501");
    CHECK(comp.value().size() == 18);           // 10 + 8, not 0 / not 32

    auto up = evaluate_index_expr(tbl, "UPPER(NAME)", 0);
    REQUIRE(up.has_value());
    CHECK(up.value() == "ALPHA     ");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("index_expr: PADR/PADL/PADC fit a value to an exact width") {
    // #131 defect C — nested compositions like Upper(PadR(LTrim(NAME),10))
    // degraded to an EMPTY key because PADx was not implemented.
    auto dir = fs::temp_directory_path() / "openads_idx_expr_pad";
    auto p = stage_dbf(dir);
    {
    auto tbl = open_table(p);

    auto pad = evaluate_index_expr(tbl, "PADR('AB', 5)", 5);
    REQUIRE(pad.has_value());
    CHECK(pad.value() == "AB   ");

    auto pal = evaluate_index_expr(tbl, "PADL('AB', 5)", 5);
    REQUIRE(pal.has_value());
    CHECK(pal.value() == "   AB");

    auto pac = evaluate_index_expr(tbl, "PADC('AB', 6)", 6);
    REQUIRE(pac.has_value());
    CHECK(pac.value() == "  AB  ");

    // Truncation when longer than the requested width.
    auto tr = evaluate_index_expr(tbl, "PADR('ABCDEF', 3)", 3);
    REQUIRE(tr.has_value());
    CHECK(tr.value() == "ABC");

    // The issue's exact nested composition (NAME = "ALPHA     " C(10)):
    // LTrim -> "ALPHA     " (no leading blanks) -> PadR 8 -> "ALPHA   "
    // -> Upper.
    auto nested = evaluate_index_expr(tbl, "UPPER(PADR(LTRIM(NAME),8))", 8);
    REQUIRE(nested.has_value());
    CHECK(nested.value() == "ALPHA   ");
    CHECK(nested.value().size() == 8);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}


TEST_CASE("fox_numeric_key is 8 bytes and order-preserving (FoxPro DBL2ORD)") {
    using openads::engine::fox_numeric_key;
    // Ascending values spanning negatives, zero, fractions and large
    // magnitudes. The 8-byte keys must compare byte-for-byte (unsigned,
    // i.e. std::string ordering) in the same ascending order — that is
    // what lets the CDX B+tree treat them as opaque bytes.
    const double vals[] = {
        -1e9, -1000.5, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0,
        20.0, 100.25, 1000.5, 1e9
    };
    std::string prev;
    bool first = true;
    for (double v : vals) {
        std::string key = fox_numeric_key(v);
        CHECK(key.size() == 8);
        if (!first) {
            // strict ascending byte order
            CHECK(prev < key);
        }
        prev = key;
        first = false;
    }
    // -0.0 must encode identically to +0.0.
    CHECK(fox_numeric_key(-0.0) == fox_numeric_key(0.0));
}

// ---- $ (contains / substring) operator in FOR-clause truthy evaluator ----

TEST_CASE("index_expr_truthy: $ operator — 'needle' $ haystack") {
    using openads::engine::evaluate_index_expr_truthy;
    // Stage a tiny DBF with STATUS C(5) and NAME C(10).
    // Record 1: STATUS="IEC", NAME="ALPHA"
    auto dir = fs::temp_directory_path() / "openads_idx_dollar";
    fs::create_directories(dir);
    auto p = dir / "dollar.dbf";
    fs::remove(p);

    std::vector<std::uint8_t> buf;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        buf.insert(buf.end(), b, b + n);
    };

    // Header.
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 1;                       // 1 record
    hdr[8] = 32 + 32 * 2 + 1;        // header_len = 97
    hdr[10] = 1 + 5 + 10;            // record_len = 16
    push(hdr.data(), hdr.size());

    auto field = [&](const char* name, char type, std::uint8_t len) {
        std::array<std::uint8_t, 32> fd{};
        std::strncpy(reinterpret_cast<char*>(fd.data()), name, 11);
        fd[11] = static_cast<std::uint8_t>(type);
        fd[16] = len;
        push(fd.data(), fd.size());
    };
    field("STATUS", 'C', 5);
    field("NAME",   'C', 10);
    buf.push_back(0x0D);

    // One record: not-deleted, STATUS="IEC  ", NAME="ALPHA     "
    buf.push_back(' ');
    const char status[5] = {'I','E','C',' ',' '};
    push(status, 5);
    const char name[10] = {'A','L','P','H','A',' ',' ',' ',' ',' '};
    push(name, 10);
    buf.push_back(0x1A);

    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(buf.data()),
        static_cast<std::streamsize>(buf.size()));

    {
    auto tbl = open_table(p);

    // 'IEC' $ STATUS  →  true (needle "IEC" is contained in "IEC  ")
    CHECK(evaluate_index_expr_truthy(tbl, "'IEC' $ STATUS"));

    // 'EC' $ STATUS  →  true (substring match)
    CHECK(evaluate_index_expr_truthy(tbl, "'EC' $ STATUS"));

    // 'XYZ' $ STATUS  →  false (not contained)
    CHECK_FALSE(evaluate_index_expr_truthy(tbl, "'XYZ' $ STATUS"));

    // '' $ STATUS  →  true (empty needle always matches)
    CHECK(evaluate_index_expr_truthy(tbl, "'' $ STATUS"));

    // 'ALPHA' $ NAME  →  true (field value contains needle)
    CHECK(evaluate_index_expr_truthy(tbl, "'ALPHA' $ NAME"));

    // 'BETA' $ NAME  →  false
    CHECK_FALSE(evaluate_index_expr_truthy(tbl, "'BETA' $ NAME"));
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
