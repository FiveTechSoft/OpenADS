#pragma once

#include "engine/sql_execution_budget.h"   // remote_sql_limit()
#include "util/result.h"
#include <cstddef>
#include <cstdint>

namespace openads::engine {

// Conservative preflight, not a time deadline. Estimate worst-case Cartesian
// fanout before materializing a remote join, even when WHERE may later prune.
//
// Limits (0 = unlimited) come from remote_sql_max_rows (default 100,000
// records per source table) and remote_sql_max_mb (default 64 MiB), env
// OPENADS_REMOTE_SQL_MAX_ROWS / OPENADS_REMOTE_SQL_MAX_MB overriding.
struct RemoteSqlShapeBudget {
    static std::uint64_t max_rows() {
        return remote_sql_limit("OPENADS_REMOTE_SQL_MAX_ROWS",
                                "remote_sql_max_rows", 100000);
    }
    static std::uint64_t max_bytes() {
        const std::uint64_t mb = remote_sql_limit(
            "OPENADS_REMOTE_SQL_MAX_MB", "remote_sql_max_mb", 64);
        return mb >= (std::uint64_t{1} << 43)
                   ? std::numeric_limits<std::uint64_t>::max()
                   : mb * 1024 * 1024;
    }
    const std::uint64_t kRows  = max_rows();
    const std::uint64_t kBytes = max_bytes();
    std::uint64_t combinations = 1;
    std::uint64_t width = 0;
    util::Result<void> add_source(std::uint64_t rows, std::uint64_t row_width) {
        if (rows > kRows || row_width > kBytes)
            return util::Error{7079, 0, "remote SQL source budget exceeded", ""};
        // Outer joins can emit a blank row for an empty source.
        if (rows == 0) rows = 1;
        if (combinations > kRows / rows)
            return util::Error{7079, 0, "remote SQL join fanout budget exceeded", ""};
        combinations *= rows;
        if (row_width > kBytes - width)
            return util::Error{7079, 0, "remote SQL row-width budget exceeded", ""};
        width += row_width;
        if (combinations != 0 && width > kBytes / combinations)
            return util::Error{7079, 0, "remote SQL materialization budget exceeded", ""};
        return {};
    }
};

// Additive staging allowance for UNION members, including rows which a later
// DISTINCT would discard. Enforce before copying rows into the accumulator.
struct RemoteSqlUnionBudget {
    std::uint64_t rows = 0;
    std::uint64_t bytes = 0;
    util::Result<void> add(std::uint64_t count, std::uint64_t width) {
        const auto max_rows  = RemoteSqlShapeBudget::max_rows();
        const auto max_bytes = RemoteSqlShapeBudget::max_bytes();
        if (count > max_rows - rows || width > max_bytes ||
            (count != 0 && width > (max_bytes - bytes) / count))
            return util::Error{7079, 0, "remote SQL UNION budget exceeded", ""};
        rows += count;
        bytes += count * width;
        return {};
    }
};

} // namespace openads::engine
