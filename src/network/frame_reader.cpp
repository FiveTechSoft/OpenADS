#include "network/frame_reader.h"

namespace openads::network {

util::Result<std::vector<Frame>> FrameReader::feed(const std::uint8_t* data,
                                                   std::size_t n, std::size_t max_frames) {
    if (n != 0) buf_.insert(buf_.end(), data, data + n);

    std::vector<Frame> out;
    std::size_t off = 0;  // bytes consumed from the front of buf_
    for (;;) {
        const std::size_t avail = buf_.size() - off;
        if (avail < 5) break;  // not even a full header yet

        const std::uint8_t* p = buf_.data() + off;
        const std::uint32_t len =
            (static_cast<std::uint32_t>(p[0]) << 24) |
            (static_cast<std::uint32_t>(p[1]) << 16) |
            (static_cast<std::uint32_t>(p[2]) <<  8) |
             static_cast<std::uint32_t>(p[3]);
        if (!valid_opcode(p[4]))
            return util::Error{5000, 0, "unknown opcode", ""};
        if (len > payload_limit_ || len > kMaxFramePayload) {
            return util::Error{5000, 0, "frame payload too large", ""};
        }
        if (avail < 5 + static_cast<std::size_t>(len)) break;  // body incomplete

        std::size_t consumed = 0;
        auto fr = decode_frame(p, avail, &consumed);
        if (!fr) return fr.error();
        out.push_back(std::move(fr.value()));
        off += consumed;
        if (max_frames != 0 && out.size() >= max_frames) break;
    }

    // Drop the consumed prefix, keep any partial trailing frame.
    if (off > 0) buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(off));
    return out;
}

} // namespace openads::network
