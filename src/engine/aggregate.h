#pragma once

#include <cstdint>
#include <string>

namespace openads::engine {

// Tier-3 server-side aggregate function selector. Wire-stable: the
// numeric values double as the [u8 fn_type] field of the Aggregate
// request frame, so do not renumber.
enum class AggFn : std::uint8_t {
    Count = 0,
    Sum   = 1,
    Avg   = 2,
    Min   = 3,
    Max   = 4,
};

// Result discriminator carried in the AggregateAck frame ([u8 result_type]).
enum class AggType : std::uint8_t {
    Empty   = 0,   // no value (zero matched rows for AVG / MIN / MAX)
    Numeric = 1,   // `bytes` is an ASCII decimal (parse with VAL())
    String  = 2,   // `bytes` is the raw field value (MIN/MAX of a text field)
};

struct AggValue {
    AggType     type = AggType::Empty;
    std::string bytes;
};

// One requested aggregate: a function plus the column it folds (empty field
// means COUNT(*)). Shared by the wire client and the SQL-backend push-down.
struct AggSpec {
    AggFn       fn = AggFn::Count;
    std::string field;
};

// Format a double as a compact, round-trippable ASCII decimal: no
// exponent, no trailing-zero noise, integral values print without a
// decimal point. Hides binary float noise (0.1+0.2 -> "0.3").
std::string format_agg_double(double v);

// Single-pass accumulator for one aggregate over a scanned table. The scan
// driver (server handler or in-process path) evaluates the FOR predicate and
// feeds this one value per matching row; `finalize()` yields the scalar.
//
// `numeric` selects how MIN/MAX compare and report the field: numerically
// (as_double) or lexicographically over the raw field bytes (as_string).
// SUM/AVG always use the numeric value; COUNT ignores both.
class AggAccumulator {
public:
    AggAccumulator(AggFn fn, bool numeric) noexcept;

    // Fold one row that already passed the FOR predicate.
    //   is_null : the field is NULL/blank for this row (skipped by every fn
    //             except COUNT(*), where the caller passes is_null=false).
    //   num     : numeric value of the field (DbfFieldValue::as_double).
    //   str     : string value of the field (DbfFieldValue::as_string).
    void feed(bool is_null, double num, const std::string& str);

    AggValue finalize() const;

    AggFn fn() const noexcept { return fn_; }

private:
    AggFn         fn_;
    bool          numeric_;
    std::uint64_t count_   = 0;     // contributing (counted / summed) rows
    double        num_acc_ = 0.0;   // running sum or numeric min/max
    bool          has_num_ = false;
    std::string   str_acc_;         // running string min/max (raw bytes)
    bool          has_str_ = false;
};

} // namespace openads::engine
