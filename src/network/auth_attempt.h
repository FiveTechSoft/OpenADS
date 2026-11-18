#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace openads::network {

// State only. Caller supplies 32 CSPRNG bytes after encrypted handshake.
// Not a database session; no opaque blob decoding or authorization here.
// Owned by one connection; caller serializes access.
class AuthAttempt {
public:
    using Clock = std::chrono::steady_clock;
    using Challenge = std::array<std::uint8_t, 32>;
    enum class State { Empty, Pending, Consumed, Cancelled, Expired };
    static constexpr std::size_t kMaxBlobBytes = 4096;

    AuthAttempt() = default;
    AuthAttempt(const AuthAttempt&) = delete;
    AuthAttempt& operator=(const AuthAttempt&) = delete;
    ~AuthAttempt() { wipe(); }

    bool begin(const Challenge& challenge, bool encrypted_ready,
               Clock::time_point now, std::chrono::seconds lifetime) noexcept {
        if (state_ != State::Empty || !encrypted_ready || lifetime.count() <= 0 ||
            lifetime > std::chrono::seconds(60)) return false;
        challenge_ = challenge;
        deadline_ = now + lifetime;
        state_ = State::Pending;
        return true;
    }

    // Consume BEFORE calling untrusted application code. Every attempt,
    // including bad-length input, uses the challenge once. Returned bytes
    // are public but must not be reused for another connection.
    bool take(Clock::time_point now, std::size_t blob_bytes,
              Challenge& out) noexcept {
        out.fill(0);
        if (state_ != State::Pending) return false;
        if (now >= deadline_) {
            state_ = State::Expired; wipe(); return false;
        }
        state_ = State::Consumed;
        const bool valid = blob_bytes > 0 && blob_bytes <= kMaxBlobBytes;
        if (valid) out = challenge_;
        wipe();
        return valid;
    }

    void cancel() noexcept {
        if (state_ == State::Empty || state_ == State::Pending)
            state_ = State::Cancelled;
        wipe();
    }
    State state() const noexcept { return state_; }

private:
    void wipe() noexcept {
        volatile std::uint8_t* p = challenge_.data();
        for (std::size_t i = 0; i < challenge_.size(); ++i) p[i] = 0;
    }
    State state_ = State::Empty;
    Challenge challenge_{};
    Clock::time_point deadline_{};
};

} // namespace openads::network
