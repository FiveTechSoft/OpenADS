#include "engine/dd_native.h"

#include "openads/error.h"

#include <cstring>

namespace openads::engine {

namespace {

constexpr std::uint8_t kMagic[4] = {'O', 'A', 'D', 'D'};

// ---- writers ----------------------------------------------------------------
void put_u16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>( v       & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>( v        & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}
void put_sstr(std::vector<std::uint8_t>& b, const std::string& s) {
    put_u16(b, static_cast<std::uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
void put_str(std::vector<std::uint8_t>& b, const std::string& s) {
    put_u32(b, static_cast<std::uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

// ---- bounds-checked reader --------------------------------------------------
struct Reader {
    const std::uint8_t* p;
    std::size_t         n;
    std::size_t         pos = 0;
    bool                ok  = true;

    bool need(std::size_t k) {
        if (!ok || pos + k > n) { ok = false; return false; }
        return true;
    }
    std::uint16_t u16() {
        if (!need(2)) return 0;
        std::uint16_t v = static_cast<std::uint16_t>(p[pos]) |
                          (static_cast<std::uint16_t>(p[pos + 1]) << 8);
        pos += 2; return v;
    }
    std::uint32_t u32() {
        if (!need(4)) return 0;
        std::uint32_t v = static_cast<std::uint32_t>(p[pos]) |
                          (static_cast<std::uint32_t>(p[pos + 1]) <<  8) |
                          (static_cast<std::uint32_t>(p[pos + 2]) << 16) |
                          (static_cast<std::uint32_t>(p[pos + 3]) << 24);
        pos += 4; return v;
    }
    std::string sstr() {
        std::uint16_t len = u16();
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + pos), len);
        pos += len; return s;
    }
    std::string str() {
        std::uint32_t len = u32();
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p + pos), len);
        pos += len; return s;
    }
};

}  // namespace

bool dd_is_native(const std::uint8_t* data, std::size_t len) noexcept {
    return data != nullptr && len >= 4 &&
           std::memcmp(data, kMagic, 4) == 0;
}

std::vector<std::uint8_t>
dd_native_encode(const std::vector<NativeSection>& sections) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), kMagic, kMagic + 4);
    put_u16(out, kDdNativeVersion);
    put_u16(out, 0);  // flags
    put_u32(out, static_cast<std::uint32_t>(sections.size()));

    for (const auto& s : sections) {
        put_sstr(out, s.name);
        // Build the body separately so we can prefix its exact byte length —
        // that length is what lets a reader skip a section it does not know.
        std::vector<std::uint8_t> body;
        put_u32(body, static_cast<std::uint32_t>(s.columns.size()));
        for (const auto& c : s.columns) put_sstr(body, c);
        put_u32(body, static_cast<std::uint32_t>(s.rows.size()));
        for (const auto& row : s.rows) {
            for (std::size_t ci = 0; ci < s.columns.size(); ++ci) {
                put_str(body, ci < row.size() ? row[ci] : std::string());
            }
        }
        put_u32(out, static_cast<std::uint32_t>(body.size()));
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

util::Result<std::vector<NativeSection>>
dd_native_decode(const std::uint8_t* data, std::size_t len) {
    if (!dd_is_native(data, len)) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "not an OADD native dictionary", ""};
    }
    Reader r{data, len, 4};  // skip magic
    std::uint16_t version = r.u16();
    (void)r.u16();           // flags (reserved)
    if (version == 0 || version > kDdNativeVersion) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "unsupported OADD version", ""};
    }
    std::uint32_t nsections = r.u32();

    std::vector<NativeSection> out;
    out.reserve(nsections);
    for (std::uint32_t i = 0; i < nsections && r.ok; ++i) {
        NativeSection sec;
        sec.name = r.sstr();
        std::uint32_t body_len = r.u32();
        if (!r.need(body_len)) break;
        // Parse the body within its own bound, so a corrupt inner length can
        // never read past body_len (and an unknown-but-well-formed section
        // could be skipped by future readers via body_len alone).
        std::size_t body_end = r.pos + body_len;

        // Cap every reserve() by the bytes actually available so a corrupt or
        // hostile count can't request a multi-gigabyte allocation: a column
        // name costs >= 2 bytes (its u16 length), a row costs >= 4 bytes.
        auto cap = [&](std::uint32_t count, std::size_t per) -> std::size_t {
            std::size_t room = (body_end > r.pos) ? (body_end - r.pos) / per : 0;
            return count < room ? count : room;
        };
        std::uint32_t ncols = r.u32();
        sec.columns.reserve(cap(ncols, 2));
        for (std::uint32_t c = 0; c < ncols && r.ok; ++c) {
            sec.columns.push_back(r.sstr());
        }
        std::uint32_t nrows = r.u32();
        sec.rows.reserve(cap(nrows, ncols ? ncols * std::size_t(4) : 4));
        for (std::uint32_t ri2 = 0; ri2 < nrows && r.ok; ++ri2) {
            std::vector<std::string> row;
            row.reserve(ncols);
            for (std::uint32_t c = 0; c < ncols && r.ok; ++c) {
                row.push_back(r.str());
            }
            sec.rows.push_back(std::move(row));
        }
        // Re-anchor to the declared section end regardless of what the body
        // parse consumed — tolerates trailing bytes / future extra fields.
        if (r.ok && r.pos <= body_end) r.pos = body_end;
        out.push_back(std::move(sec));
    }
    if (!r.ok) {
        return util::Error{openads::AE_INTERNAL_ERROR, 0,
                           "truncated or malformed OADD dictionary", ""};
    }
    return out;
}

}  // namespace openads::engine
