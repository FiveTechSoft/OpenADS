#pragma once
#include <thread>
#include <utility>

namespace openads::network::detail {
// Allocate the registry slot BEFORE starting a joinable thread. Otherwise
// insertion failure could destroy a temporary joinable thread and terminate.
// Caller holds the session registry mutex throughout.
template <typename Map, typename Launch>
void register_session_thread(Map& threads, typename Map::key_type id,
                             Launch&& launch) {
    auto entry = threads.try_emplace(id);
    try {
        entry.first->second = launch();
    } catch (...) {
        threads.erase(entry.first);
        throw;
    }
}
} // namespace openads::network::detail
