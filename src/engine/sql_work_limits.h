#pragma once

#include "util/result.h"
#include <cstddef>
#include <cstdint>

namespace openads::engine {

// Conservative preflight, not a time deadline. Estimate worst-case Cartesian
// fanout before materializing a remote join, even when WHERE may later prune.
struct RemoteSqlShapeBudget {
    static constexpr std::uint64_t kRows = 100000;
    static constexpr std::uint64_t kBytes = 64 * 1024 * 1024;
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

} // namespace openads::engine
