#pragma once

#include "engine/data_dict.h"
#include "engine/repl_queue.h"
#include "engine/repl_catalog.h"
#include "session/connection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::engine {

// Best-effort replication capture. Called after a table mutation is
// committed. Never returns a failing Result to the writer.
void repl_capture_row(session::Connection* c, Table& t, ReplRecType type,
                      const std::vector<std::uint8_t>* before,
                      const std::vector<std::uint8_t>* after);

// Emit TX_BEGIN / TX_COMMIT / TX_ABORT into the queue.
void repl_capture_tx(session::Connection* c, ReplRecType type,
                     std::uint64_t tx_id);

// Atomic counter of enqueue failures (monitored by operators).
std::uint64_t repl_enqueue_failures();

} // namespace openads::engine
