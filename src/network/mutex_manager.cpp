#include "network/mutex_manager.h"

namespace openads::network {

bool MutexManager::create(const std::string& name) {
    std::lock_guard<std::mutex> lk(mu_);
    if (mutexes_.count(name)) return false;
    mutexes_[name] = std::make_shared<MutexState>();
    return true;
}

bool MutexManager::lock(const std::string& name, std::uint32_t timeout_ms,
                        const std::string& owner) {
    std::shared_ptr<MutexState> ms;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = mutexes_.find(name);
        if (it == mutexes_.end()) return false;
        ms = it->second;
    }
    std::unique_lock<std::mutex> lk(ms->mu);
    if (timeout_ms == 0) {
        // Wait forever
        ms->cv.wait(lk, [&] { return !ms->locked; });
    } else {
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        if (!ms->cv.wait_until(lk, deadline, [&] { return !ms->locked; })) {
            return false;  // timeout
        }
    }
    ms->locked = true;
    ms->owner = owner;
    return true;
}

bool MutexManager::try_lock(const std::string& name,
                            const std::string& owner) {
    std::shared_ptr<MutexState> ms;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = mutexes_.find(name);
        if (it == mutexes_.end()) return false;
        ms = it->second;
    }
    std::lock_guard<std::mutex> lk(ms->mu);
    if (ms->locked) return false;
    ms->locked = true;
    ms->owner = owner;
    return true;
}

bool MutexManager::unlock(const std::string& name,
                          const std::string& owner) {
    std::shared_ptr<MutexState> ms;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = mutexes_.find(name);
        if (it == mutexes_.end()) return false;
        ms = it->second;
    }
    std::lock_guard<std::mutex> lk(ms->mu);
    if (!ms->locked || ms->owner != owner) return false;
    ms->locked = false;
    ms->owner.clear();
    ms->cv.notify_one();
    return true;
}

bool MutexManager::destroy(const std::string& name,
                           const std::string& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = mutexes_.find(name);
    if (it == mutexes_.end()) return false;
    auto& ms = it->second;
    {
        std::lock_guard<std::mutex> mlk(ms->mu);
        if (ms->locked && !ms->owner.empty() && ms->owner != owner) {
            return false;  // locked by someone else
        }
    }
    mutexes_.erase(it);
    return true;
}

bool MutexManager::exists(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    return mutexes_.count(name) > 0;
}

bool MutexManager::is_locked(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = mutexes_.find(name);
    if (it == mutexes_.end()) return false;
    std::lock_guard<std::mutex> mlk(it->second->mu);
    return it->second->locked;
}

std::string MutexManager::get_owner(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = mutexes_.find(name);
    if (it == mutexes_.end()) return {};
    std::lock_guard<std::mutex> mlk(it->second->mu);
    return it->second->owner;
}

void MutexManager::release_all(const std::string& owner) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [name, ms] : mutexes_) {
        std::lock_guard<std::mutex> mlk(ms->mu);
        if (ms->locked && ms->owner == owner) {
            ms->locked = false;
            ms->owner.clear();
            ms->cv.notify_one();
        }
    }
}

} // namespace openads::network
