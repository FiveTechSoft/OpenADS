// tests/unit/aggregate_test.cpp
// Tier-3 server-side aggregation. AggAccumulator folds the field values of the
// rows that already passed the FOR predicate into a single COUNT / SUM / AVG /
// MIN / MAX scalar, so a totalling scan returns one value over the wire instead
// of dragging every matching row to the client. Pure logic, no table/wire — the
// scan driver (server handler or local path) feeds it one value per matched row.
#include "doctest.h"
#include "engine/aggregate.h"

#include <string>

using openads::engine::AggAccumulator;
using openads::engine::AggFn;
using openads::engine::AggType;
using openads::engine::format_agg_double;

TEST_CASE("COUNT(*) counts every matched row") {
    AggAccumulator a(AggFn::Count, /*numeric=*/false);
    for (int i = 0; i < 5; ++i) a.feed(false, 0.0, "");
    auto r = a.finalize();
    CHECK(r.type == AggType::Numeric);
    CHECK(r.bytes == "5");
}

TEST_CASE("COUNT over zero rows is numeric 0") {
    AggAccumulator a(AggFn::Count, false);
    auto r = a.finalize();
    CHECK(r.type == AggType::Numeric);
    CHECK(r.bytes == "0");
}

TEST_CASE("COUNT(field) skips nulls") {
    AggAccumulator a(AggFn::Count, true);
    a.feed(false, 1.0, "1");
    a.feed(true,  0.0, "");   // null -> not counted
    a.feed(false, 2.0, "2");
    CHECK(a.finalize().bytes == "2");
}

TEST_CASE("SUM adds numeric field values") {
    AggAccumulator a(AggFn::Sum, true);
    a.feed(false, 100.0, ""); a.feed(false, 50.5, ""); a.feed(false, 0.25, "");
    auto r = a.finalize();
    CHECK(r.type == AggType::Numeric);
    CHECK(r.bytes == "150.75");
}

TEST_CASE("SUM over zero rows is 0 (xBase semantics)") {
    AggAccumulator a(AggFn::Sum, true);
    auto r = a.finalize();
    CHECK(r.type == AggType::Numeric);
    CHECK(r.bytes == "0");
}

TEST_CASE("SUM skips nulls") {
    AggAccumulator a(AggFn::Sum, true);
    a.feed(false, 10.0, "10");
    a.feed(true,  99.0, "");   // null ignored
    a.feed(false, 5.0, "5");
    CHECK(a.finalize().bytes == "15");
}

TEST_CASE("AVG is sum over non-null count") {
    AggAccumulator a(AggFn::Avg, true);
    a.feed(false, 10.0, ""); a.feed(false, 20.0, ""); a.feed(false, 30.0, "");
    CHECK(a.finalize().bytes == "20");
}

TEST_CASE("AVG over zero rows is null/empty") {
    AggAccumulator a(AggFn::Avg, true);
    CHECK(a.finalize().type == AggType::Empty);
}

TEST_CASE("MIN/MAX over a numeric field") {
    AggAccumulator mn(AggFn::Min, true), mx(AggFn::Max, true);
    for (double v : {42.0, 7.0, 99.5, 7.0, 13.0}) {
        mn.feed(false, v, ""); mx.feed(false, v, "");
    }
    CHECK(mn.finalize().type == AggType::Numeric);
    CHECK(mn.finalize().bytes == "7");
    CHECK(mx.finalize().bytes == "99.5");
}

TEST_CASE("MIN/MAX over a string field compares lexically, returns raw bytes") {
    AggAccumulator mn(AggFn::Min, false), mx(AggFn::Max, false);
    for (const std::string& s : {std::string("MARIA"),
                                 std::string("ANA"),
                                 std::string("ZED")}) {
        mn.feed(false, 0.0, s); mx.feed(false, 0.0, s);
    }
    CHECK(mn.finalize().type == AggType::String);
    CHECK(mn.finalize().bytes == "ANA");
    CHECK(mx.finalize().bytes == "ZED");
}

TEST_CASE("MIN/MAX over zero rows is empty") {
    AggAccumulator mn(AggFn::Min, true);
    CHECK(mn.finalize().type == AggType::Empty);
    AggAccumulator mxs(AggFn::Max, false);
    CHECK(mxs.finalize().type == AggType::Empty);
}

TEST_CASE("MIN skips nulls (a null must not become the extreme)") {
    AggAccumulator mn(AggFn::Min, true);
    mn.feed(true, -999.0, "");   // null
    mn.feed(false, 5.0, "");
    mn.feed(false, 8.0, "");
    CHECK(mn.finalize().bytes == "5");
}

TEST_CASE("format_agg_double trims trailing zeros and float noise") {
    CHECK(format_agg_double(150.0)      == "150");
    CHECK(format_agg_double(150.75)     == "150.75");
    CHECK(format_agg_double(0.0)        == "0");
    CHECK(format_agg_double(-12.5)      == "-12.5");
    CHECK(format_agg_double(0.1 + 0.2)  == "0.3");   // 0.30000000000000004 -> 0.3
}
