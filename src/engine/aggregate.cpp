#include "engine/aggregate.h"

#include <cstdio>
#include <string>

namespace openads::engine {

std::string format_agg_double(double v) {
    if (v == 0.0) return "0";   // also normalises -0.0
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s(buf);
    // Trim trailing zeros (and a bare trailing dot). The fixed %.6f form
    // never uses an exponent, so this is purely cosmetic clean-up that also
    // hides binary float noise (0.1+0.2 -> "0.300000" -> "0.3").
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos) {
        std::size_t last = s.find_last_not_of('0');
        if (last == dot) --last;     // drop the now-bare decimal point
        s.erase(last + 1);
    }
    return s;
}

AggAccumulator::AggAccumulator(AggFn fn, bool numeric) noexcept
    : fn_(fn), numeric_(numeric) {}

void AggAccumulator::feed(bool is_null, double num, const std::string& str) {
    // Every function ignores NULL/blank fields. COUNT(*) routes through here
    // with is_null=false so it counts every matched row; COUNT(field) passes
    // the real null flag so only non-null fields contribute.
    if (is_null) return;
    ++count_;
    switch (fn_) {
        case AggFn::Count:
            break;
        case AggFn::Sum:
        case AggFn::Avg:
            num_acc_ += num;
            break;
        case AggFn::Min:
            if (numeric_) {
                if (!has_num_ || num < num_acc_) num_acc_ = num;
                has_num_ = true;
            } else {
                if (!has_str_ || str < str_acc_) str_acc_ = str;
                has_str_ = true;
            }
            break;
        case AggFn::Max:
            if (numeric_) {
                if (!has_num_ || num > num_acc_) num_acc_ = num;
                has_num_ = true;
            } else {
                if (!has_str_ || str > str_acc_) str_acc_ = str;
                has_str_ = true;
            }
            break;
    }
}

AggValue AggAccumulator::finalize() const {
    switch (fn_) {
        case AggFn::Count:
            return {AggType::Numeric, std::to_string(count_)};
        case AggFn::Sum:
            // xBase SUM over zero rows is 0, not null.
            return {AggType::Numeric, format_agg_double(num_acc_)};
        case AggFn::Avg:
            if (count_ == 0) return {};   // null / empty
            return {AggType::Numeric,
                    format_agg_double(num_acc_ /
                                      static_cast<double>(count_))};
        case AggFn::Min:
        case AggFn::Max:
            if (numeric_) {
                if (!has_num_) return {};
                return {AggType::Numeric, format_agg_double(num_acc_)};
            }
            if (!has_str_) return {};
            return {AggType::String, str_acc_};
    }
    return {};   // unreachable
}

} // namespace openads::engine
