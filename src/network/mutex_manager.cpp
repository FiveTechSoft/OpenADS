#include "network/mutex_manager.h"

#include <vector>

namespace openads::network {

bool MutexManager::create(const std::string& name,
                         const std::string& creator) {
    std::lock_guard<std::mutex> lk(mu_);
    if (mutexes_.count(name)) return false;
    auto ms = std::make_shared<MutexState>();
    ms->creator = creator;
    mutexes_[name] = std::move(ms);
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

void MutexManager::release_session(const std::string& owner) {
    if (owner.empty()) return;
    std::vector<std::string> drop;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [name, ms] : mutexes_) {
            std::lock_guard<std::mutex> mlk(ms->mu);
            // 1) Free locks held by the dying session so peers waiting
            //    on them (finite or infinite timeout) wake promptly.
            if (ms->locked && ms->owner == owner) {
                ms->locked = false;
                ms->owner.clear();
                ms->cv.notify_all();
            }
            // 2) Reap names the dying session created. A live peer
            //    holding the lock keeps it: creation transfers to the
            //    holder so the name still dies with its last user.
            if (ms->creator == owner) {
                if (ms->locked && !ms->owner.empty() && ms->owner != owner) {
                    ms->creator = ms->owner;
                } else {
                    ms->cv.notify_all();
                    drop.push_back(name);
                }
            }
        }
        for (auto& name : drop) mutexes_.erase(name);
    }
}

} // namespace openads::network
