#pragma once

#include "engine/data_dict.h"
#include "engine/repl_queue.h"

#include <cstdint>
#include <string>

namespace openads::engine {

// Drain the replication queue for a single subscription, applying
// pending records to the local target. Phase 1: target_uri is a
// filesystem path (directory containing the target table).
// Returns the number of records applied, or an error.
struct ReplApplyResult {
    std::uint64_t records_applied = 0;
    std::uint64_t last_lsn_applied = 0;
};

util::Result<ReplApplyResult>
    repl_apply_once(DataDict& dd,
                    const std::string& queue_path,
                    const std::string& subscription_name);

} // namespace openads::engine
