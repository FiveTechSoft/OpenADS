#pragma once

#include "session/connection.h"
#include "session/handle_registry.h"

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace openads::abi::detail {

// Process-wide state shared by ACE entry points. Keep this as the sole owner
// while ace_exports.cpp is split into domain translation units.
struct ProcessState {
    // Recursive because SQL UNION execution deliberately re-enters
    // AdsExecuteSQLDirect while the outer entry point holds this lock.
    std::recursive_mutex                                    mu;
    session::HandleRegistry                                 registry;
    std::unordered_map<session::Handle,
                       std::unique_ptr<session::Connection>> conns;
    std::vector<std::unique_ptr<session::Connection::TableFind>> remote_finds;
};

ProcessState& state();

} // namespace openads::abi::detail
