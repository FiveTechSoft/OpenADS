#pragma once
#include "network/auth_attempt.h"
#include "network/auth_result.h"
#include <memory>
#include <atomic>
#include <string>
namespace openads::network {
class AuthWorker {
public:
    static constexpr unsigned kDefaultConcurrency = 4;
    static constexpr unsigned kMaxConcurrency = 256;
    AuthWorker(std::string executable, std::string module,
               unsigned concurrency = kDefaultConcurrency);
    bool probe();
    bool authenticate(const AuthAttempt::Challenge&, const std::string& blob,
                      const std::string& context, AuthResult&, bool* capacity_busy = nullptr);
private:
    bool run(const std::string& input, std::string& output, bool* capacity_busy = nullptr);
    const unsigned concurrency_;
    std::atomic<unsigned> active_{0};
    std::string executable_,module_;
};
}
