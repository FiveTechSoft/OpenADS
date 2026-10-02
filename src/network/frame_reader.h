#pragma once

#include "network/wire.h"
#include "util/result.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openads::network {

// Incremental de-framer for non-blocking reads. Feed it whatever bytes a single
// recv() produced; it buffers any partial frame internally and returns every
// complete frame it can decode so far. A frame whose declared payload exceeds
// kMaxFramePayload yields an error (the caller should drop the connection).
//
// This keeps the risky reassembly logic socket-free and unit-testable; the
// pooled read path is just "recv available bytes -> feed() -> dispatch each".
class FrameReader {
public:
    util::Result<std::vector<Frame>> feed(const std::uint8_t* data,
                                          std::size_t n, std::size_t max_frames = 0);
    void set_payload_limit(std::size_t limit) noexcept { payload_limit_ = limit; }

    // Bytes currently held (the prefix of an incomplete frame).
    std::size_t buffered() const noexcept { return buf_.size(); }

private:
    std::vector<std::uint8_t> buf_;
    std::size_t payload_limit_ = kMaxFramePayload;
};

} // namespace openads::network
