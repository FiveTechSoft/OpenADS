#include "session/handle_registry.h"

namespace openads::session {

Handle HandleRegistry::register_object(HandleKind kind, void* ptr) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    Handle h = next_++;
    slots_[h] = Slot{kind, ptr};
    return h;
}

void HandleRegistry::release(Handle h) {
    std::unique_lock<std::shared_mutex> lk(mu_);
    slots_.erase(h);
}

} // namespace openads::session
