#include "engine/vf_service.h"
#include <mutex>
namespace openads::engine::vf {
namespace { std::mutex factory_mu; Factory service_factory; }
void set_factory(Factory f) { std::lock_guard<std::mutex> guard(factory_mu); service_factory=std::move(f); }
std::shared_ptr<Service> make_service() {
    Factory f;
    { std::lock_guard<std::mutex> guard(factory_mu); f=service_factory; }
    return f ? f() : nullptr;
}
}
