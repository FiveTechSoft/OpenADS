#pragma once
#include "network/vf_codec.h"
#include <functional>
#include <memory>
namespace openads::engine::vf {
class Service {
public:
    virtual ~Service() = default;
    virtual network::vf::Reply execute(const network::vf::Request&) = 0;
};
using Factory = std::function<std::shared_ptr<Service>()>;
void set_factory(Factory factory);
std::shared_ptr<Service> make_service();
} // namespace openads::engine::vf
