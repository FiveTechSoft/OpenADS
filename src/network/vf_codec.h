#pragma once

#include "openads/vf_wire.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace openads::network::vf {
inline std::uint64_t get(const std::uint8_t* p, unsigned n) {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v |= std::uint64_t(p[i]) << (i * 8);
    return v;
}
inline void put(std::vector<std::uint8_t>& out, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}
inline std::int64_t signed_value(std::uint64_t u) {
    // Do not rely on implementation-defined unsigned-to-signed conversion.
    std::int64_t s;
    static_assert(sizeof(s) == sizeof(u), "64-bit wire integer required");
    std::memcpy(&s, &u, sizeof(s));
    return s;
}
struct Request {
    std::uint8_t op = 0;
    std::uint32_t handle = 0, flags = 0;
    std::int64_t value1 = 0, value2 = 0;
    std::string path1, path2;
    std::string root; // server-only validated namespace, never serialized
    std::string client_path1; // original spelling, also never serialized
    std::vector<std::uint8_t> data;
};
struct Reply {
    std::uint32_t error = 0;
    std::int64_t value1 = 0, value2 = 0;
    std::vector<std::uint8_t> data;
};
inline bool is_extension(const std::vector<std::uint8_t>& in) {
    return in.size() >= 4 && std::memcmp(in.data(), "OVF1", 4) == 0;
}
inline bool decode(const std::vector<std::uint8_t>& in, Request& out) {
    if (in.size() < OADS_VF_REQUEST_SIZE || !is_extension(in) ||
        in[4] != OADS_VF_VERSION || in[5] < OADS_VF_OPEN ||
        in[5] > OADS_VF_BLOBREAD || get(in.data()+6,2) != 0) return false;
    const auto p1 = get(in.data()+32,4), p2 = get(in.data()+36,4), n = get(in.data()+40,4);
    if (p1 > OADS_VF_PATH_MAX || p2 > OADS_VF_PATH_MAX || n > OADS_VF_CHUNK_MAX ||
        p1+p2+n != in.size()-OADS_VF_REQUEST_SIZE) return false;
    Request r;
    r.op = in[5]; r.handle = static_cast<std::uint32_t>(get(in.data()+8,4));
    r.value1 = signed_value(get(in.data()+12,8)); r.value2 = signed_value(get(in.data()+20,8));
    r.flags = static_cast<std::uint32_t>(get(in.data()+28,4));
    const auto* start = reinterpret_cast<const char*>(in.data()+OADS_VF_REQUEST_SIZE);
    r.path1.assign(start,static_cast<std::size_t>(p1));
    r.path2.assign(start+p1,static_cast<std::size_t>(p2));
    if (r.path1.find('\0') != std::string::npos || r.path2.find('\0') != std::string::npos) return false;
    r.data.assign(in.begin()+static_cast<std::ptrdiff_t>(OADS_VF_REQUEST_SIZE+p1+p2),in.end());
    out = std::move(r);
    return true;
}
inline bool encode(const Request& r, std::vector<std::uint8_t>& out) {
    if (r.op < OADS_VF_OPEN || r.op > OADS_VF_BLOBREAD ||
        r.path1.size() > OADS_VF_PATH_MAX || r.path2.size() > OADS_VF_PATH_MAX ||
        r.data.size() > OADS_VF_CHUNK_MAX || r.path1.find('\0') != std::string::npos ||
        r.path2.find('\0') != std::string::npos) return false;
    std::vector<std::uint8_t> v{'O','V','F','1',OADS_VF_VERSION,r.op,0,0};
    put(v,r.handle,4); put(v,static_cast<std::uint64_t>(r.value1),8);
    put(v,static_cast<std::uint64_t>(r.value2),8); put(v,r.flags,4);
    put(v,r.path1.size(),4); put(v,r.path2.size(),4); put(v,r.data.size(),4);
    v.insert(v.end(),r.path1.begin(),r.path1.end()); v.insert(v.end(),r.path2.begin(),r.path2.end());
    v.insert(v.end(),r.data.begin(),r.data.end()); out = std::move(v);
    return true;
}
inline bool decode(const std::vector<std::uint8_t>& in, Reply& out) {
    if (in.size() < OADS_VF_REPLY_SIZE ||
        get(in.data()+20,4) != in.size()-OADS_VF_REPLY_SIZE ||
        in.size()-OADS_VF_REPLY_SIZE > OADS_VF_CHUNK_MAX) return false;
    Reply r;
    r.error = static_cast<std::uint32_t>(get(in.data(),4));
    r.value1 = signed_value(get(in.data()+4,8)); r.value2 = signed_value(get(in.data()+12,8));
    r.data.assign(in.begin()+OADS_VF_REPLY_SIZE,in.end()); out = std::move(r);
    return true;
}
inline bool encode(const Reply& r, std::vector<std::uint8_t>& out) {
    if (r.data.size() > OADS_VF_CHUNK_MAX) return false;
    std::vector<std::uint8_t> v;
    put(v,r.error,4); put(v,static_cast<std::uint64_t>(r.value1),8);
    put(v,static_cast<std::uint64_t>(r.value2),8); put(v,r.data.size(),4);
    v.insert(v.end(),r.data.begin(),r.data.end()); out = std::move(v);
    return true;
}
} // namespace openads::network::vf
