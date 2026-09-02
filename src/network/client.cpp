#include "network/client.h"

#include "abi/lock_retry_policy.h"
#include "engine/table.h"
#include "openads/error.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace openads::network {

namespace {

inline void write_u32_le(std::uint32_t v,
                         std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

inline std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void write_u16_le(std::uint16_t v,
                         std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
}

inline std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8));
}

// M12.29 — [u16 len][bytes] string helper shared by the DD RPC methods.
inline void write_lstr16(const std::string& s, std::vector<std::uint8_t>& out) {
    write_u16_le(static_cast<std::uint16_t>(s.size()), out);
    out.insert(out.end(), s.begin(), s.end());
}

// M12.33 — read a [u16 len][bytes] string from a payload buffer.
inline bool read_lstr16(const std::vector<std::uint8_t>& pl,
                        std::size_t& off, std::string& out) {
    if (off + 2 > pl.size()) return false;
    std::uint16_t len = read_u16_le(pl.data() + off);
    off += 2;
    if (off + len > pl.size()) return false;
    out.assign(reinterpret_cast<const char*>(pl.data() + off), len);
    off += len;
    return true;
}

} // namespace

// M12.18 — parse the per-row trailer the server appends to every
// nav-op ack and the FetchCurrentRow ack. Format:
//
//   [u8 has_row]
//   if has_row != 0:
//     [u32 recno][u8 deleted][u16 nfields]
//     per field: [u32 len][bytes]
//
// On success rt is updated in-place; on a "no row" trailer (has_row
// == 0) rt->row_valid is cleared and current_row left as is.
namespace {

// Parse one record body starting at `pos`: [u32 recno][u8 deleted]
// [u16 nfields][per-field: u32 len, bytes]. Returns the new cursor
// position past the record, or std::size_t(-1) on truncation.
std::size_t parse_one_row(const std::vector<std::uint8_t>& pl,
                           std::size_t pos,
                           std::uint32_t& recno,
                           bool& deleted,
                           std::vector<std::string>& fields) {
    constexpr std::size_t fail = static_cast<std::size_t>(-1);
    if (pos + 4 + 1 + 2 > pl.size()) return fail;
    recno   = read_u32_le(&pl[pos]); pos += 4;
    deleted = (pl[pos++] != 0);
    std::uint16_t n = read_u16_le(&pl[pos]); pos += 2;
    if (n > kMaxWireFields) return fail;
    fields.clear();
    fields.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        if (pos + 4 > pl.size()) return fail;
        std::uint32_t vlen = read_u32_le(&pl[pos]); pos += 4;
        if (pos + vlen > pl.size()) return fail;
        fields.emplace_back(
            reinterpret_cast<const char*>(pl.data() + pos), vlen);
        pos += vlen;
    }
    return pos;
}

// RCB 07/15/2026: `block_dir` is the direction the SERVER walked the lookahead
// block (+1 forward, -1 backward) — which is sign(eff), NOT sign(the caller's
// step. On a direction reversal those disagree for one step: after a forward
// run the resync eff can still be positive on the first backward Skip, so the
// server sends a FORWARD block; tagging it backward would make the next
// Skip(-1) pop a forward row and serve the wrong record. Callers that never
// request a block (GotoBottom / Seek / FetchCurrentRow) can leave the default;
// if no block comes back the queue ends up empty and prefetch_dir is set to 0.
void parse_row_trailer_into(RemoteTable* rt,
                             const std::vector<std::uint8_t>& pl,
                             std::size_t pos = 0,
                             std::int8_t block_dir = 1) {
    if (rt == nullptr || pos >= pl.size()) {
        if (rt) rt->row_valid = false;
        return;
    }
    rt->prefetch_queue.clear();
    // M12.21 option C — every nav ack re-anchors the server cursor to the
    // client's logical position, so the lag resets to zero.
    rt->cursor_lag   = 0;
    rt->prefetch_dir = 0;
    std::uint8_t has_row = pl[pos++];
    if (has_row == 0) {
        rt->row_valid = false;
        // Optional lookahead count (always 0 when has_row=0, but
        // accept the trailing 2 bytes for protocol stability).
        return;
    }
    auto end = parse_one_row(pl, pos,
        rt->current_recno, rt->current_deleted, rt->current_row);
    if (end == static_cast<std::size_t>(-1)) {
        rt->row_valid = false; return;
    }
    pos = end;
    rt->row_valid = true;
    // M12.21 lookahead block. [u16 count] then count rows.
    if (pos + 2 > pl.size()) return;     // M12.18 server (no lookahead) — done.
    std::uint16_t la = read_u16_le(&pl[pos]); pos += 2;
    for (std::uint16_t i = 0; i < la; ++i) {
        RemoteTable::PrefetchedRow pr;
        end = parse_one_row(pl, pos, pr.recno, pr.deleted, pr.fields);
        if (end == static_cast<std::size_t>(-1)) break;
        pos = end;
        rt->prefetch_queue.push_back(std::move(pr));
    }
    if (!rt->prefetch_queue.empty()) rt->prefetch_dir = block_dir;
}

} // namespace

namespace {

bool parse_scheme_uri(const std::string& uri,
                      const std::string& scheme,
                      std::string& host,
                      std::uint16_t& port,
                      std::string& data_dir) {
    if (uri.size() < scheme.size() ||
        uri.compare(0, scheme.size(), scheme) != 0) {
        return false;
    }
    std::size_t after = scheme.size();
    std::size_t slash = uri.find('/', after);
    std::string hostport = uri.substr(after,
        slash == std::string::npos ? std::string::npos : slash - after);
    std::size_t colon = hostport.find(':');
    if (colon == std::string::npos) return false;
    host = hostport.substr(0, colon);
    port = static_cast<std::uint16_t>(
        std::strtoul(hostport.substr(colon + 1).c_str(), nullptr, 10));
    data_dir = (slash == std::string::npos) ? std::string()
                                            : uri.substr(slash + 1);
    return true;
}

} // namespace

bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir) {
    return parse_scheme_uri(uri, "tcp://", host, port, data_dir);
}

bool parse_tls_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir) {
    return parse_scheme_uri(uri, "tls://", host, port, data_dir);
}

util::Result<Frame> RemoteConnection::request(const Frame& f) {
    std::lock_guard<std::mutex> lk(mu_);
    // Fail fast on a disconnected handle. rddads hands us connection
    // handles that AdsDisconnect already tore down (its "current
    // connection" can point at a handle another thread just closed);
    // dereferencing a null transport_ here was an access violation,
    // and a blocking read on a dead socket hung the caller forever.
    // SAP ADS returns an error immediately in this state.
    if (!transport_ || !transport_->valid()) {
        return util::Error{5036 /* AE_NO_CONNECTION */, 0,
                           "RemoteConnection: not connected", ""};
    }
    if (auto r = write_frame(*transport_,f); !r) return r.error();
    auto rep = read_frame(*transport_);
    if (!rep) return rep.error();
    // M12.10 — Error frame payload prefixed with [u32 LE ace_code].
    // Parse it back into the util::Error so callers see the real ACE
    // code (5036, 7077, 5066, ...) instead of a generic 5000.
    if (rep.value().opcode == Opcode::Error) {
        std::uint32_t code = 5000;
        std::string   msg;
        const auto&   pl = rep.value().payload;
        if (pl.size() >= 4) {
            code = read_u32_le(pl.data());
            msg.assign(reinterpret_cast<const char*>(pl.data() + 4),
                       pl.size() - 4);
        } else {
            msg.assign(reinterpret_cast<const char*>(pl.data()),
                       pl.size());
        }
        return util::Error{static_cast<std::int32_t>(code), 0,
                           std::move(msg), ""};
    }
    return rep;
}

namespace {

void connect_pack_payload(std::vector<std::uint8_t>& payload,
                          const std::string& data_dir,
                          const std::string& user,
                          const std::string& password) {
    auto pushlen = [](std::vector<std::uint8_t>& out, std::uint16_t n) {
        out.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
        out.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    };
    auto pushstr = [&pushlen](std::vector<std::uint8_t>& out,
                              const std::string& s) {
        pushlen(out, static_cast<std::uint16_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    };
    pushstr(payload, data_dir);
    pushstr(payload, user);
    pushstr(payload, password);
    // M12.21 option C — advertise the prefetch-consume capability so the
    // server may piggyback lookahead rows on forward-Skip acks. Trailing
    // and optional: pre-M12.21 servers ignore the extra 4 bytes.
    // RCB 07/15/2026: M12.25 — also advertise backward (PgUp) prefetch. This is
    // a distinct bit precisely because a server must NOT send a backward block
    // to a client that only understands forward ones (see kCapPrefetchBackward).
    std::uint32_t caps = kCapPrefetchConsume | kCapPrefetchBackward
                       | kCapOpenTableMode;
    for (int i = 0; i < 4; ++i)
        payload.push_back(static_cast<std::uint8_t>((caps >> (8 * i)) & 0xFFu));
}

} // namespace

util::Result<void> RemoteConnection::connect(const std::string& host,
                                              std::uint16_t port,
                                              const std::string& data_dir,
                                              const std::string& user,
                                              const std::string& password) {
    auto s = connect_tcp(host, port);
    if (!s) return s.error();
    return connect_with_transport(make_plain_transport(s.value()),
                                  data_dir, user, password);
}

util::Result<void>
RemoteConnection::connect_with_transport(std::unique_ptr<ITransport> transport,
                                          const std::string& data_dir,
                                          const std::string& user,
                                          const std::string& password) {
    if (!transport || !transport->valid()) {
        return util::Error{5000, 0,
            "RemoteConnection: invalid transport", data_dir};
    }
    transport_ = std::move(transport);
    Frame req;
    req.opcode = Opcode::Connect;
    connect_pack_payload(req.payload, data_dir, user, password);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ConnectAck) {
        std::string msg(rep.value().payload.begin(),
                        rep.value().payload.end());
        return util::Error{5000, 0, "Connect: " + msg, data_dir};
    }
    // M12.32 — SET DELETED often runs before AdsConnect60 (rddads apps
    // set it in Main, then connect), so the AdsShowDeleted broadcast
    // found no remote connection to notify. Sync the state now; the
    // server default is "show", so only the hidden state needs pushing.
    if (!openads::engine::show_deleted()) show_deleted(false);
    return {};
}

void RemoteConnection::disconnect() noexcept {
    // Serialise with request(): resetting transport_ while another
    // thread is mid-round-trip on this connection made that thread
    // dereference a null transport_ (AV) or read a corrupted stream.
    std::lock_guard<std::mutex> lk(mu_);
    if (!transport_ || !transport_->valid()) return;
    Frame req;
    req.opcode = Opcode::Disconnect;
    (void)write_frame(*transport_, req);
    transport_->close();
    transport_.reset();
}

util::Result<void> RemoteConnection::begin_transaction() {
    Frame req;
    req.opcode = Opcode::BeginTransaction;
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::BeginTransactionAck) {
        return util::Error{5000, 0, "BeginTransaction: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    return {};
}

util::Result<void> RemoteConnection::commit_transaction() {
    Frame req;
    req.opcode = Opcode::CommitTransaction;
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CommitTransactionAck) {
        return util::Error{5000, 0, "CommitTransaction: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    return {};
}

util::Result<void> RemoteConnection::rollback_transaction() {
    Frame req;
    req.opcode = Opcode::RollbackTransaction;
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::RollbackTransactionAck) {
        return util::Error{5000, 0, "RollbackTransaction: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    return {};
}

util::Result<RemoteConnection::OpenTableResult>
RemoteConnection::open_table(const std::string& rel, std::uint16_t mode) {
    Frame req;
    req.opcode = Opcode::OpenTable;
    // Extended payload: [u16 mode][table_name_bytes]
    // Old servers ignore the prefix and read the full payload as the table
    // name; new servers strip the 2-byte prefix when mode is non-zero.
    write_u16_le(mode, req.payload);
    req.payload.insert(req.payload.end(), rel.begin(), rel.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::OpenTableAck) {
        return util::Error{5000, 0, "OpenTable: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    const auto& pl = rep.value().payload;
    if (pl.size() < 4) {
        return util::Error{5000, 0,
            "OpenTable: ack payload too short", ""};
    }
    OpenTableResult result;
    result.id = read_u32_le(pl.data());
    // Parse optional production bag path appended by the server.
    // Old servers send exactly 4 bytes; new servers append:
    //   [u16 bag_len][bag_bytes]
    size_t off = 4;
    if (off + 2 <= pl.size()) {
        std::uint16_t blen = read_u16_le(pl.data() + off);
        off += 2;
        if (blen > 0 && off + blen <= pl.size()) {
            result.prod_bag_path.assign(
                reinterpret_cast<const char*>(pl.data() + off), blen);
        }
    }
    return result;
}

util::Result<void> RemoteConnection::close_table(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::CloseTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CloseTableAck) {
        return util::Error{5000, 0, "CloseTable: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::goto_top(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GotoTop;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoTopAck) {
        return util::Error{5000, 0, "GotoTop: server error", ""};
    }
    return {};
}

// M12.18 — RemoteTable-aware overload: same wire op, but parses
// the row trailer the server appends to populate the table's
// row cache in-place. xbrowse repaint becomes 1 RTT per Skip
// (the row arrives with the ack) instead of 2.
util::Result<void> RemoteConnection::goto_top(RemoteTable* rt) {
    // RCB 07/15/2026: rt is non-null by caller contract -- every caller resolves
    // it from the handle registry (get_remote_table) or from a ri->parent that
    // was already checked. Deliberately NOT null-checked here, matching every
    // other rt/id method on this class; a lone guard (flagged in review) would
    // be inconsistent noise, not added safety.
    Frame req;
    req.opcode = Opcode::GotoTop;
    write_u32_le(rt->id, req.payload);
    // RCB 07/14/2026: M12.24 — same OPTIONAL trailing [u16 depth] the Skip
    // request carries. GotoTop now comes back warm (a lookahead block rides on
    // the ack), and it has to respect AdsCacheRecords like everything else —
    // otherwise an app that explicitly turned read-ahead OFF would still get a
    // block dumped on it by every GoTop. Old servers length-check and ignore
    // the two bytes; see kPrefetchDepthAuto in wire.h.
    req.payload.push_back(
        static_cast<std::uint8_t>(rt->cache_records_hint & 0xFFu));
    req.payload.push_back(
        static_cast<std::uint8_t>((rt->cache_records_hint >> 8) & 0xFFu));
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoTopAck) {
        return util::Error{5000, 0, "GotoTop: server error", ""};
    }
    parse_row_trailer_into(rt, rep.value().payload, 0);
    return {};
}

util::Result<void> RemoteConnection::skip(std::uint32_t id,
                                           std::int32_t step) {
    Frame req;
    req.opcode = Opcode::Skip;
    write_u32_le(id, req.payload);
    write_u32_le(static_cast<std::uint32_t>(step), req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SkipAck) {
        return util::Error{5000, 0, "Skip: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::skip(RemoteTable* rt,
                                           std::int32_t step) {
    // RCB 07/15/2026: rt is non-null by caller contract (resolved from the
    // handle registry, or a checked ri->parent). Not null-checked, for the same
    // reason as goto_top(rt) above -- consistent with the rest of this class.
    Frame req;
    req.opcode = Opcode::Skip;
    write_u32_le(rt->id, req.payload);
    // M12.21 option C — the server cursor is offset from the client's logical
    // position by cursor_lag (rows served locally from the queue without a
    // round-trip). Fold that lag into the wire step so the server lands where
    // the client logically is + step. RCB 07/15/2026: cursor_lag is signed now
    // (negative for a backward run), so this one line serves both directions;
    // parse_row_trailer_into on the ack zeroes it again.
    std::int32_t eff = step + rt->cursor_lag;
    write_u32_le(static_cast<std::uint32_t>(eff), req.payload);
    // RCB 07/14/2026: M12.23 — trailing optional [u16 depth] carrying the
    // AdsCacheRecords value (kPrefetchDepthAuto when the app never called it).
    // Safe to append unconditionally: an older server length-checks its Skip
    // payload (`size() < 8`) and reads only the first 8 bytes, so it simply
    // ignores these two. That is why this needed no capability bit — see the
    // note on kPrefetchDepthAuto in wire.h for the other direction, which is
    // the one that could actually have bitten us.
    req.payload.push_back(
        static_cast<std::uint8_t>(rt->cache_records_hint & 0xFFu));
    req.payload.push_back(
        static_cast<std::uint8_t>((rt->cache_records_hint >> 8) & 0xFFu));
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SkipAck) {
        return util::Error{5000, 0, "Skip: server error", ""};
    }
    // The block (if any) was walked by the server in the direction of eff, not
    // of the user's step — see parse_row_trailer_into. eff == 0 sends no block.
    const std::int8_t bdir = (eff > 0) ? 1 : (eff < 0) ? -1 : 1;
    parse_row_trailer_into(rt, rep.value().payload, 0, bdir);
    return {};
}

util::Result<std::string>
RemoteConnection::get_field(std::uint32_t id,
                             const std::string& field_name) {
    Frame req;
    req.opcode = Opcode::GetField;
    write_u32_le(id, req.payload);
    req.payload.insert(req.payload.end(),
                       field_name.begin(), field_name.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetFieldAck) {
        return util::Error{5000, 0, "GetField: server error",
                           field_name};
    }
    return std::string(rep.value().payload.begin(),
                       rep.value().payload.end());
}

util::Result<std::uint32_t>
RemoteConnection::record_count(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetRecordCount;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordCountAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "GetRecordCount: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::key_count(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetKeyCount;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetKeyCountAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "GetKeyCount: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

// M12.29 — server-side key number. The server computes the position of the
// current record in the active order's walk via pos_of_recno_cached() → O(1),
// eliminating the O(n) remote_measure_keyno client-side walk.
util::Result<std::uint32_t> RemoteConnection::key_num(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetKeyNum;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetKeyNumAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "GetKeyNum: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<bool> RemoteConnection::at_eof(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::AtEOF;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AtEOFAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0, "AtEOF: server error", ""};
    }
    return rep.value().payload[0] != 0;
}

util::Result<void> RemoteConnection::append_blank(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::AppendBlank;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AppendBlankAck) {
        return util::Error{5000, 0, "AppendBlank: server error", ""};
    }
    return {};
}

util::Result<std::vector<std::uint8_t>>
RemoteConnection::get_record(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "GetRecord: server error", ""};
    }
    const auto& pl = rep.value().payload;
    std::uint16_t len = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(pl[0]) |
        (static_cast<std::uint16_t>(pl[1]) << 8));
    if (pl.size() < 2u + len) {
        return util::Error{5000, 0, "GetRecord: truncated payload", ""};
    }
    return std::vector<std::uint8_t>(pl.begin() + 2, pl.begin() + 2 + len);
}

util::Result<std::uint32_t>
RemoteConnection::get_record_crc(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetRecordCRC;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordCRAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "GetRecordCRC: server error", ""};
    }
    const auto& pl = rep.value().payload;
    return static_cast<std::uint32_t>(pl[0]) |
           (static_cast<std::uint32_t>(pl[1]) << 8) |
           (static_cast<std::uint32_t>(pl[2]) << 16) |
           (static_cast<std::uint32_t>(pl[3]) << 24);
}

util::Result<void> RemoteConnection::set_record(std::uint32_t id,
                                                const std::uint8_t* bytes,
                                                std::size_t len) {
    if (len > 0xFFFFu) {
        return util::Error{5000, 0, "SetRecord: record too large", ""};
    }
    Frame req;
    req.opcode = Opcode::SetRecord;
    write_u32_le(id, req.payload);
    auto ulen = static_cast<std::uint16_t>(len);
    write_u16_le(ulen, req.payload);
    if (bytes != nullptr && len > 0) {
        req.payload.insert(req.payload.end(), bytes, bytes + len);
    }
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetRecordAck) {
        return util::Error{5000, 0, "SetRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::set_field(std::uint32_t id,
                                                const std::string& field_name,
                                                const std::string& value) {
    if (field_name.size() > 0xFFFFu) {
        return util::Error{5000, 0,
            "SetField: field name too long", field_name};
    }
    Frame req;
    req.opcode = Opcode::SetField;
    write_u32_le(id, req.payload);
    auto nlen = static_cast<std::uint16_t>(field_name.size());
    req.payload.push_back(static_cast<std::uint8_t>( nlen        & 0xFFu));
    req.payload.push_back(static_cast<std::uint8_t>((nlen >>  8) & 0xFFu));
    req.payload.insert(req.payload.end(),
                       field_name.begin(), field_name.end());
    req.payload.insert(req.payload.end(), value.begin(), value.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetFieldAck) {
        return util::Error{5000, 0, "SetField: server error",
                           field_name};
    }
    return {};
}

util::Result<void> RemoteConnection::delete_record(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::DeleteRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DeleteRecordAck) {
        return util::Error{5000, 0, "DeleteRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::recall_record(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::RecallRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::RecallRecordAck) {
        return util::Error{5000, 0, "RecallRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::goto_record(std::uint32_t id,
                                                  std::uint32_t recno) {
    Frame req;
    req.opcode = Opcode::GotoRecord;
    write_u32_le(id, req.payload);
    write_u32_le(recno, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoRecordAck) {
        return util::Error{5000, 0, "GotoRecord: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::goto_record(RemoteTable* rt,
                                                  std::uint32_t recno) {
    Frame req;
    req.opcode = Opcode::GotoRecord;
    write_u32_le(rt->id, req.payload);
    write_u32_le(recno, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoRecordAck) {
        return util::Error{5000, 0, "GotoRecord: server error", ""};
    }
    parse_row_trailer_into(rt, rep.value().payload, 0);
    return {};
}

util::Result<void> RemoteConnection::goto_bottom(RemoteTable* rt) {
    Frame req;
    req.opcode = Opcode::GotoBottom;
    write_u32_le(rt->id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoBottomAck) {
        return util::Error{5000, 0, "GotoBottom: server error", ""};
    }
    parse_row_trailer_into(rt, rep.value().payload, 0);
    return {};
}

util::Result<void> RemoteConnection::flush_table(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::FlushTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FlushTableAck) {
        return util::Error{5000, 0, "FlushTable: server error", ""};
    }
    return {};
}

util::Result<std::uint32_t>
RemoteConnection::find_record(
    std::uint32_t id,
    const std::vector<std::pair<std::string, std::string>>& identity) {
    Frame req;
    req.opcode = Opcode::FindRecord;
    write_u32_le(id, req.payload);
    write_u16_le(static_cast<std::uint16_t>(identity.size()), req.payload);
    for (auto& [n, v] : identity) {
        write_u16_le(static_cast<std::uint16_t>(n.size()), req.payload);
        req.payload.insert(req.payload.end(), n.begin(), n.end());
        write_u16_le(static_cast<std::uint16_t>(v.size()), req.payload);
        req.payload.insert(req.payload.end(), v.begin(), v.end());
    }
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FindRecordAck) {
        return util::Error{5000, 0, "FindRecord: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    if (rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "FindRecord: ack too short", ""};
    }
    std::uint32_t recno = read_u32_le(rep.value().payload.data());
    return recno;
}

util::Result<void> RemoteConnection::reindex(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::Reindex;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ReindexAck) {
        return util::Error{5000, 0, "Reindex: server error", ""};
    }
    return {};
}

util::Result<std::vector<std::vector<std::string>>>
RemoteConnection::fetch_batch(std::uint32_t id,
                               std::uint32_t max_rows,
                               const std::vector<std::string>& columns) {
    if (columns.size() > 0xFFu) {
        return util::Error{5000, 0,
            "Fetch: too many columns (max 255)", ""};
    }
    Frame req;
    req.opcode = Opcode::Fetch;
    write_u32_le(id, req.payload);
    write_u32_le(max_rows, req.payload);
    req.payload.push_back(static_cast<std::uint8_t>(columns.size()));
    for (auto& c : columns) {
        if (c.size() > 0xFFu) {
            return util::Error{5000, 0,
                "Fetch: column name too long (max 255)", c};
        }
        req.payload.push_back(static_cast<std::uint8_t>(c.size()));
        req.payload.insert(req.payload.end(), c.begin(), c.end());
    }
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FetchAck ||
        rep.value().payload.size() < 5) {
        return util::Error{5000, 0, "Fetch: server error", ""};
    }
    const auto& pl = rep.value().payload;
    std::size_t   p = 0;
    std::uint32_t nrows = read_u32_le(pl.data() + p); p += 4;
    std::uint8_t  ncols = pl[p++];
    std::vector<std::vector<std::string>> rows;
    rows.reserve(nrows);
    for (std::uint32_t r = 0; r < nrows; ++r) {
        std::vector<std::string> row;
        row.reserve(ncols);
        for (std::uint8_t c = 0; c < ncols; ++c) {
            if (p + 2 > pl.size()) {
                return util::Error{5000, 0,
                    "Fetch: truncated payload (vlen)", ""};
            }
            std::uint16_t vlen = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(pl[p]) |
                (static_cast<std::uint32_t>(pl[p + 1]) << 8));
            p += 2;
            if (p + vlen > pl.size()) {
                return util::Error{5000, 0,
                    "Fetch: truncated payload (val)", ""};
            }
            row.emplace_back(reinterpret_cast<const char*>(pl.data() + p),
                             vlen);
            p += vlen;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

util::Result<FetchWhereBatch>
RemoteConnection::fetch_where(std::uint32_t id,
                              std::uint32_t max_rows,
                              const std::string& where_expr,
                              const std::vector<std::string>& columns,
                              std::uint8_t flags) {
    if (columns.size() > 0xFFu) {
        return util::Error{5000, 0,
            "FetchWhere: too many columns (max 255)", ""};
    }
    if (where_expr.size() > 0xFFFFu) {
        return util::Error{5000, 0,
            "FetchWhere: predicate too long (max 65535)", ""};
    }
    Frame req;
    req.opcode = Opcode::FetchWhere;
    write_u32_le(id, req.payload);
    write_u32_le(max_rows, req.payload);
    req.payload.push_back(flags);       // new: flags byte at offset 8
    req.payload.push_back(
        static_cast<std::uint8_t>( where_expr.size()       & 0xFFu));
    req.payload.push_back(
        static_cast<std::uint8_t>((where_expr.size() >> 8) & 0xFFu));
    req.payload.insert(req.payload.end(),
                       where_expr.begin(), where_expr.end());
    req.payload.push_back(static_cast<std::uint8_t>(columns.size()));
    for (auto& c : columns) {
        if (c.size() > 0xFFu) {
            return util::Error{5000, 0,
                "FetchWhere: column name too long (max 255)", c};
        }
        req.payload.push_back(static_cast<std::uint8_t>(c.size()));
        req.payload.insert(req.payload.end(), c.begin(), c.end());
    }
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FetchWhereAck ||
        rep.value().payload.size() < 5) {
        return util::Error{5000, 0, "FetchWhere: server error", ""};
    }
    const auto& pl = rep.value().payload;
    std::size_t   p = 0;
    std::uint32_t nrows = read_u32_le(pl.data() + p); p += 4;
    std::uint8_t  ncols = pl[p++];
    FetchWhereBatch batch;
    batch.rows.reserve(nrows);
    if (flags & FetchWhereFlags::WANT_RECNO)
        batch.recnos.reserve(nrows);
    for (std::uint32_t r = 0; r < nrows; ++r) {
        // Per-row optional recno (emitted before column data).
        if (flags & FetchWhereFlags::WANT_RECNO) {
            if (p + 4 > pl.size()) {
                return util::Error{5000, 0,
                    "FetchWhere: truncated payload (recno)", ""};
            }
            std::uint32_t rn = read_u32_le(pl.data() + p); p += 4;
            batch.recnos.push_back(rn);
        }
        std::vector<std::string> row;
        row.reserve(ncols);
        for (std::uint8_t c = 0; c < ncols; ++c) {
            if (p + 2 > pl.size()) {
                return util::Error{5000, 0,
                    "FetchWhere: truncated payload (vlen)", ""};
            }
            std::uint16_t vlen = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(pl[p]) |
                (static_cast<std::uint32_t>(pl[p + 1]) << 8));
            p += 2;
            if (p + vlen > pl.size()) {
                return util::Error{5000, 0,
                    "FetchWhere: truncated payload (val)", ""};
            }
            row.emplace_back(reinterpret_cast<const char*>(pl.data() + p),
                             vlen);
            p += vlen;
        }
        batch.rows.push_back(std::move(row));
    }
    // Trailing [u8 eof] byte: 1 = the server walked to end of table.
    if (p < pl.size()) {
        batch.eof = (pl[p] != 0);
    }
    return batch;
}

util::Result<AggregateBatch>
RemoteConnection::aggregate(std::uint32_t               id,
                            const std::string&          for_expr,
                            const std::vector<AggSpec>& specs) {
    if (specs.size() > 0xFFu)
        return util::Error{5000, 0,
            "Aggregate: too many aggregates (max 255)", ""};
    if (for_expr.size() > 0xFFFFu)
        return util::Error{5000, 0,
            "Aggregate: predicate too long (max 65535)", ""};
    Frame req;
    req.opcode = Opcode::Aggregate;
    write_u32_le(id, req.payload);
    req.payload.push_back(
        static_cast<std::uint8_t>( for_expr.size()       & 0xFFu));
    req.payload.push_back(
        static_cast<std::uint8_t>((for_expr.size() >> 8) & 0xFFu));
    req.payload.insert(req.payload.end(), for_expr.begin(), for_expr.end());
    req.payload.push_back(static_cast<std::uint8_t>(specs.size()));
    for (const auto& s : specs) {
        if (s.field.size() > 0xFFu)
            return util::Error{5000, 0,
                "Aggregate: field name too long (max 255)", s.field};
        req.payload.push_back(static_cast<std::uint8_t>(s.fn));
        req.payload.push_back(static_cast<std::uint8_t>(s.field.size()));
        req.payload.insert(req.payload.end(), s.field.begin(), s.field.end());
    }
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AggregateAck ||
        rep.value().payload.empty())
        return util::Error{5000, 0, "Aggregate: server error", ""};

    const auto& pl = rep.value().payload;
    std::size_t  p = 0;
    std::uint8_t n = pl[p++];
    AggregateBatch out;
    out.values.reserve(n);
    for (std::uint8_t i = 0; i < n; ++i) {
        if (p + 3 > pl.size())
            return util::Error{5000, 0,
                "Aggregate: truncated payload (header)", ""};
        std::uint8_t  rt   = pl[p++];
        std::uint16_t vlen = static_cast<std::uint16_t>(
            static_cast<std::uint32_t>(pl[p]) |
            (static_cast<std::uint32_t>(pl[p + 1]) << 8));
        p += 2;
        if (p + vlen > pl.size())
            return util::Error{5000, 0,
                "Aggregate: truncated payload (value)", ""};
        engine::AggValue v;
        v.type = static_cast<engine::AggType>(rt);
        v.bytes.assign(reinterpret_cast<const char*>(pl.data() + p), vlen);
        p += vlen;
        out.values.push_back(std::move(v));
    }
    return out;
}

util::Result<std::uint32_t>
RemoteConnection::execute_sql(const std::string& sql) {
    Frame req;
    req.opcode = Opcode::ExecuteSQL;
    req.payload.assign(sql.begin(), sql.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ExecuteSQLAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "ExecuteSQL: server error",
                           sql.substr(0, 200)};
    }
    return read_u32_le(rep.value().payload.data());
}

// =====================================================================
// M12.14 — remote field metadata + extended cursor state.
// =====================================================================

util::Result<std::vector<RemoteConnection::FieldDesc>>
RemoteConnection::describe_table(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::DescribeTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DescribeTableAck) {
        return util::Error{5000, 0,
            "DescribeTable: server error", ""};
    }
    const auto& pl = rep.value().payload;
    if (pl.size() < 2) {
        return util::Error{5000, 0,
            "DescribeTable: short payload", ""};
    }
    std::vector<FieldDesc> out;
    std::size_t pos = 0;
    std::uint16_t n = read_u16_le(&pl[pos]); pos += 2;
    out.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        if (pos >= pl.size()) {
            return util::Error{5000, 0,
                "DescribeTable: truncated field record", ""};
        }
        std::uint8_t name_len = pl[pos++];
        if (pos + name_len + 8 > pl.size()) {
            return util::Error{5000, 0,
                "DescribeTable: truncated field record", ""};
        }
        FieldDesc f;
        f.name.assign(pl.begin() + static_cast<std::ptrdiff_t>(pos),
                      pl.begin() + static_cast<std::ptrdiff_t>(pos + name_len));
        pos += name_len;
        f.type     = read_u16_le(&pl[pos]); pos += 2;
        f.length   = read_u32_le(&pl[pos]); pos += 4;
        f.decimals = read_u16_le(&pl[pos]); pos += 2;
        out.push_back(std::move(f));
    }
    return out;
}

util::Result<bool> RemoteConnection::at_bof(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::AtBOF;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::AtBOFAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0, "AtBOF: server error", ""};
    }
    return rep.value().payload[0] != 0;
}

util::Result<std::uint32_t>
RemoteConnection::get_record_num(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GetRecordNum;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordNumAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0,
            "GetRecordNum: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<bool>
RemoteConnection::is_record_deleted(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::IsRecordDeleted;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::IsRecordDeletedAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0,
            "IsRecordDeleted: server error", ""};
    }
    return rep.value().payload[0] != 0;
}

util::Result<void> RemoteConnection::goto_bottom(std::uint32_t id) {
    Frame req;
    req.opcode = Opcode::GotoBottom;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GotoBottomAck) {
        return util::Error{5000, 0, "GotoBottom: server error", ""};
    }
    return {};
}

// =====================================================================
// M12.15 — info / lock / maintenance / AOF.
//
// Pattern: every op carries a u32 server table id in the request
// payload; the ack carries the answer (bool / u16 / u32) or is
// empty for void. The server-side handlers in network/server.cpp
// match the same wire layout.
// =====================================================================

util::Result<bool> RemoteConnection::is_found(std::uint32_t id) {
    Frame req; req.opcode = Opcode::IsFound;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::IsFoundAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0, "IsFound: server error", ""};
    }
    return rep.value().payload[0] != 0;
}

util::Result<void> RemoteConnection::refresh_record(std::uint32_t id) {
    Frame req; req.opcode = Opcode::RefreshRecord;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::RefreshRecordAck) {
        return util::Error{5000, 0, "RefreshRecord: server error", ""};
    }
    return {};
}

util::Result<std::uint16_t>
RemoteConnection::get_table_type(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetTableType;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetTableTypeAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "GetTableType: server error", ""};
    }
    return read_u16_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::get_record_length(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetRecordLength;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetRecordLengthAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "GetRecordLength: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<std::uint16_t>
RemoteConnection::get_num_indexes(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetNumIndexes;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetNumIndexesAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "GetNumIndexes: server error", ""};
    }
    return read_u16_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::get_last_autoinc(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetLastAutoinc;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetLastAutoincAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "GetLastAutoinc: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::get_last_table_update(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetLastTableUpdate;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetLastTableUpdateAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "GetLastTableUpdate: server error", ""};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<void> RemoteConnection::lock_record(std::uint32_t id,
                                                  std::uint32_t recno) {
    // The server answers a contended lock immediately (fail-fast). The
    // retry lives HERE: one wire request per attempt, so the connection
    // mutex is free between attempts and a peer thread's unlock/lock ops
    // interleave instead of starving behind ours (Pritpal Bedi:
    // "dbUnlock() in threads fail somehow").
    const auto policy = openads::abi::lock_retry_policy();
    // Budget = retry_count x cycle_ms (the plain ACE total-wait contract),
    // but early attempts recheck after a few ms (adaptive backoff): a
    // short contention — the common case — resolves in single-digit ms
    // instead of one 100ms slice (700-instance B_BIG measured a full
    // ~1.4s per contended RLock with the flat quantum).
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline =
        t0 + std::chrono::milliseconds(policy.budget_ms());
    for (std::uint32_t i = 0; ; ++i) {
        Frame req; req.opcode = Opcode::LockRecord;
        write_u32_le(id, req.payload);
        write_u32_le(recno, req.payload);
        auto rep = request(req);
        if (rep && rep.value().opcode == Opcode::LockRecordAck) return {};
        // Only contention (AE_LOCKED) is retryable — the server fail-fast
        // answers it via an Error frame, which request() maps to a failed
        // Result. Anything else is a real error; return it at once.
        const bool contended =
            !rep && rep.error().code ==
                        static_cast<std::int32_t>(openads::AE_LOCKED);
        if (!contended) {
            if (rep) {
                return util::Error{5000, 0, "LockRecord: server error", ""};
            }
            return rep.error();
        }
        if (i >= policy.retry_count &&
            std::chrono::steady_clock::now() >= deadline) {
            return rep.error();
        }
        openads::abi::lock_retry_sleep(i);
    }
}

util::Result<void> RemoteConnection::unlock_record(std::uint32_t id,
                                                    std::uint32_t recno) {
    Frame req; req.opcode = Opcode::UnlockRecord;
    write_u32_le(id, req.payload);
    write_u32_le(recno, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::UnlockRecordAck) {
        return util::Error{5000, 0, "UnlockRecord: server error", ""};
    }
    return {};
}

util::Result<std::uint16_t> RemoteConnection::is_record_locked(
        std::uint32_t id, std::uint32_t recno) {
    Frame req; req.opcode = Opcode::IsRecordLocked;
    write_u32_le(id, req.payload);
    write_u32_le(recno, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::IsRecordLockedAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "IsRecordLocked: server error", ""};
    }
    return read_u16_le(rep.value().payload.data());
}

util::Result<std::vector<std::uint32_t>> RemoteConnection::get_all_locks(
        std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetAllLocks;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetAllLocksAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "GetAllLocks: server error", ""};
    }
    const auto& pl = rep.value().payload;
    std::uint16_t n = read_u16_le(pl.data());
    if (pl.size() < 2 + static_cast<std::size_t>(n) * 4) {
        return util::Error{5000, 0, "GetAllLocks: short payload", ""};
    }
    std::vector<std::uint32_t> recs;
    recs.reserve(n);
    for (std::uint16_t i = 0; i < n; ++i) {
        recs.push_back(read_u32_le(pl.data() + 2 + i * 4));
    }
    return recs;
}

util::Result<void> RemoteConnection::lock_table(std::uint32_t id) {
    // Same client-side retry as lock_record (see there): budget =
    // retry_count x cycle_ms with adaptive early backoff.
    const auto policy = openads::abi::lock_retry_policy();
    const auto t0 = std::chrono::steady_clock::now();
    const auto deadline =
        t0 + std::chrono::milliseconds(policy.budget_ms());
    for (std::uint32_t i = 0; ; ++i) {
        Frame req; req.opcode = Opcode::LockTable;
        write_u32_le(id, req.payload);
        auto rep = request(req);
        if (rep && rep.value().opcode == Opcode::LockTableAck) return {};
        const bool contended =
            !rep && rep.error().code ==
                        static_cast<std::int32_t>(openads::AE_LOCKED);
        if (!contended) {
            if (rep) {
                return util::Error{5000, 0, "LockTable: server error", ""};
            }
            return rep.error();
        }
        if (i >= policy.retry_count &&
            std::chrono::steady_clock::now() >= deadline) {
            return rep.error();
        }
        openads::abi::lock_retry_sleep(i);
    }
}

util::Result<void> RemoteConnection::unlock_table(std::uint32_t id) {
    Frame req; req.opcode = Opcode::UnlockTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::UnlockTableAck) {
        return util::Error{5000, 0, "UnlockTable: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::pack_table(std::uint32_t id) {
    Frame req; req.opcode = Opcode::PackTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::PackTableAck) {
        return util::Error{5000, 0, "PackTable: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::zap_table(std::uint32_t id) {
    Frame req; req.opcode = Opcode::ZapTable;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ZapTableAck) {
        return util::Error{5000, 0, "ZapTable: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::flush_file_buffers(std::uint32_t id) {
    Frame req; req.opcode = Opcode::FlushFileBuffers;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FlushFileBuffersAck) {
        return util::Error{5000, 0, "FlushFileBuffers: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::close_all_indexes(std::uint32_t id) {
    Frame req; req.opcode = Opcode::CloseAllIndexes;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CloseAllIndexesAck) {
        return util::Error{5000, 0, "CloseAllIndexes: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::set_aof(std::uint32_t id,
                                              const std::string& cond) {
    Frame req; req.opcode = Opcode::SetAOF;
    write_u32_le(id, req.payload);
    req.payload.insert(req.payload.end(), cond.begin(), cond.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetAOFAck) {
        return util::Error{5000, 0, "SetAOF: server error",
                           cond.substr(0, 200)};
    }
    return {};
}

util::Result<void> RemoteConnection::clear_aof(std::uint32_t id) {
    Frame req; req.opcode = Opcode::ClearAOFRemote;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ClearAOFRemoteAck) {
        return util::Error{5000, 0, "ClearAOF: server error", ""};
    }
    return {};
}

util::Result<void> RemoteConnection::customize_aof(
    std::uint32_t id, std::uint16_t option,
    const std::vector<std::uint32_t>& recnos) {
    if (recnos.size() > 0xFFFFu) {
        return util::Error{5000, 0, "CustomizeAOF: too many records", ""};
    }
    Frame req;
    req.opcode = Opcode::CustomizeAOF;
    write_u32_le(id, req.payload);
    req.payload.push_back(static_cast<std::uint8_t>(option & 0xFFu));
    auto n = static_cast<std::uint16_t>(recnos.size());
    write_u16_le(n, req.payload);
    for (auto r : recnos)
        write_u32_le(r, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CustomizeAOFAck) {
        return util::Error{5000, 0, "CustomizeAOF: server error", ""};
    }
    return {};
}

util::Result<std::uint16_t>
RemoteConnection::get_aof_opt_level(std::uint32_t id) {
    Frame req; req.opcode = Opcode::GetAOFOptLevel;
    write_u32_le(id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetAOFOptLevelAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0,
            "GetAOFOptLevel: server error", ""};
    }
    return read_u16_le(rep.value().payload.data());
}

// =====================================================================
// M12.16 — remote index handle subsystem.
// =====================================================================

util::Result<std::vector<RemoteConnection::OpenIndexEntry>>
RemoteConnection::open_index(std::uint32_t table_id,
                              const std::string& path) {
    Frame req; req.opcode = Opcode::OpenIndex;
    write_u32_le(table_id, req.payload);
    req.payload.insert(req.payload.end(), path.begin(), path.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::OpenIndexAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "OpenIndex: server error", path};
    }
    const auto& pl = rep.value().payload;
    std::uint16_t n = read_u16_le(pl.data());
    if (pl.size() < 2u + 4u * n) {
        return util::Error{5000, 0,
            "OpenIndex: short payload", path};
    }
    std::vector<OpenIndexEntry> out;
    out.reserve(n);
    const size_t legacy_end = 2u + 4u * n;
    if (pl.size() == legacy_end) {
        for (std::uint16_t i = 0; i < n; ++i) {
            OpenIndexEntry e;
            e.id = read_u32_le(pl.data() + 2 + 4u * i);
            out.push_back(std::move(e));
        }
        return out;
    }
    size_t off = 2;
    // bag_path is appended after all tag entries by the server so
    // AdsGetIndexFilename / OrdBagName can serve without a wire
    // round-trip.  Parse it after the tag loop.
    std::string bag_path;
    for (std::uint16_t i = 0; i < n; ++i) {
        if (off + 6 > pl.size()) {
            return util::Error{5000, 0,
                "OpenIndex: short tag payload", path};
        }
        OpenIndexEntry e;
        e.id = read_u32_le(pl.data() + off);
        off += 4;
        std::uint16_t tlen = read_u16_le(pl.data() + off);
        off += 2;
        if (off + tlen > pl.size()) {
            return util::Error{5000, 0,
                "OpenIndex: truncated tag name", path};
        }
        if (tlen > 0) {
            e.tag.assign(reinterpret_cast<const char*>(pl.data() + off),
                         tlen);
            while (!e.tag.empty() && e.tag.back() == ' ') e.tag.pop_back();
        }
        off += tlen;
        out.push_back(std::move(e));
    }
    // Parse trailing bag path (u16-prefixed) if present. Older servers
    // don't emit it, so the payload may end here — that's fine.
    if (off + 2 <= pl.size()) {
        std::uint16_t blen = read_u16_le(pl.data() + off);
        off += 2;
        if (blen > 0 && off + blen <= pl.size()) {
            bag_path.assign(reinterpret_cast<const char*>(pl.data() + off),
                            blen);
        }
        // Advance past the bag_path bytes. Previously missing: harmless
        // while bag_path was the last field, but it left `off` pointing
        // mid-string for anything parsed afterward — e.g. the per-tag
        // expression/unique/descending block below.
        off += blen;
    }
    // Propagate the bag path to every entry so the caller can store it
    // on each RemoteIndex without special-casing.
    if (!bag_path.empty()) {
        for (auto& e : out) e.bag_path = bag_path;
    }
    // Optional trailing per-tag metadata (expression, unique, descending),
    // one triple per entry in the same order as the tag loop above. Added
    // after bag_path so older servers that don't emit it still parse fine —
    // the loop below simply finds no bytes left and leaves the defaults.
    for (std::uint16_t i = 0; i < n; ++i) {
        if (off + 2 > pl.size()) break;
        std::uint16_t elen = read_u16_le(pl.data() + off);
        off += 2;
        if (off + elen > pl.size()) break;
        if (elen > 0) {
            out[i].expression.assign(
                reinterpret_cast<const char*>(pl.data() + off), elen);
        }
        off += elen;
        if (off + 2 > pl.size()) break;
        out[i].is_unique     = pl[off]     != 0;
        out[i].is_descending = pl[off + 1] != 0;
        off += 2;
    }
    return out;
}

util::Result<void> RemoteConnection::close_index(std::uint32_t index_id) {
    Frame req; req.opcode = Opcode::CloseIndex;
    write_u32_le(index_id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CloseIndexAck) {
        return util::Error{5000, 0, "CloseIndex: server error", ""};
    }
    return {};
}

// =====================================================================
// M12.29 — AdsDD* Data Dictionary property API, phase 1.
// =====================================================================

util::Result<std::string> RemoteConnection::dd_get_property(
    DDObjectKind kind, const std::string& name, const std::string& subName,
    std::uint16_t propId) {
    Frame req; req.opcode = Opcode::DDGetProperty;
    req.payload.push_back(static_cast<std::uint8_t>(kind));
    write_lstr16(name, req.payload);
    write_lstr16(subName, req.payload);
    write_u16_le(propId, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDGetPropertyAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "DDGetProperty: server error", name};
    }
    const auto& pl = rep.value().payload;
    std::uint32_t len = read_u32_le(pl.data());
    if (pl.size() < 4u + len) {
        return util::Error{5000, 0, "DDGetProperty: short payload", name};
    }
    return std::string(reinterpret_cast<const char*>(pl.data() + 4), len);
}

util::Result<void> RemoteConnection::dd_set_property(
    DDObjectKind kind, const std::string& name, const std::string& subName,
    std::uint16_t propId, const std::string& value) {
    Frame req; req.opcode = Opcode::DDSetProperty;
    req.payload.push_back(static_cast<std::uint8_t>(kind));
    write_lstr16(name, req.payload);
    write_lstr16(subName, req.payload);
    write_u16_le(propId, req.payload);
    write_u32_le(static_cast<std::uint32_t>(value.size()), req.payload);
    req.payload.insert(req.payload.end(), value.begin(), value.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDSetPropertyAck) {
        return util::Error{5000, 0, "DDSetProperty: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_proc(
    const std::string& name, const std::string& container,
    const std::string& procName, const std::string& inParams,
    const std::string& outParams, const std::string& comments) {
    Frame req; req.opcode = Opcode::DDCreateProc;
    write_lstr16(name, req.payload);
    write_lstr16(container, req.payload);
    write_lstr16(procName, req.payload);
    write_lstr16(inParams, req.payload);
    write_lstr16(outParams, req.payload);
    write_lstr16(comments, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateProcAck) {
        return util::Error{5000, 0, "DDCreateProc: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_function(
    const std::string& name, const std::string& container,
    const std::string& implementation, const std::string& retType,
    const std::string& inParams, const std::string& comment) {
    Frame req; req.opcode = Opcode::DDCreateFunction;
    write_lstr16(name, req.payload);
    write_lstr16(container, req.payload);
    write_lstr16(implementation, req.payload);
    write_lstr16(retType, req.payload);
    write_lstr16(inParams, req.payload);
    write_lstr16(comment, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateFunctionAck) {
        return util::Error{5000, 0, "DDCreateFunction: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_trigger(
    const std::string& name, const std::string& table, std::uint32_t type,
    const std::string& container, const std::string& procedure,
    std::uint32_t priority) {
    Frame req; req.opcode = Opcode::DDCreateTrigger;
    write_lstr16(name, req.payload);
    write_lstr16(table, req.payload);
    write_u32_le(type, req.payload);
    write_lstr16(container, req.payload);
    write_lstr16(procedure, req.payload);
    write_u32_le(priority, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateTriggerAck) {
        return util::Error{5000, 0, "DDCreateTrigger: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_drop_trigger(const std::string& name) {
    Frame req; req.opcode = Opcode::DDDropTrigger;
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDDropTriggerAck) {
        return util::Error{5000, 0, "DDDropTrigger: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_drop_view(const std::string& name) {
    Frame req; req.opcode = Opcode::DDDropView;
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDDropViewAck) {
        return util::Error{5000, 0, "DDDropView: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_drop_link(const std::string& name) {
    Frame req; req.opcode = Opcode::DDDropLink;
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDDropLinkAck) {
        return util::Error{5000, 0, "DDDropLink: server error", name};
    }
    return {};
}

// =====================================================================
// M12.30 — AdsDD* Data Dictionary property API, phase 2.
// =====================================================================

util::Result<void> RemoteConnection::dd_create_user(
    const std::string& group, const std::string& user,
    const std::string& pwd, const std::string& desc) {
    Frame req; req.opcode = Opcode::DDCreateUser;
    write_lstr16(group, req.payload);
    write_lstr16(user, req.payload);
    write_lstr16(pwd, req.payload);
    write_lstr16(desc, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateUserAck) {
        return util::Error{5000, 0, "DDCreateUser: server error", user};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_drop_object(
    DDObjectKind kind, const std::string& name) {
    Frame req; req.opcode = Opcode::DDDropObject;
    req.payload.push_back(static_cast<std::uint8_t>(kind));
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDDropObjectAck) {
        return util::Error{5000, 0, "DDDropObject: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_add_user_to_group(
    const std::string& group, const std::string& user) {
    Frame req; req.opcode = Opcode::DDAddUserToGroup;
    write_lstr16(group, req.payload);
    write_lstr16(user, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDAddUserToGroupAck) {
        return util::Error{5000, 0, "DDAddUserToGroup: server error", user};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_remove_user_from_group(
    const std::string& group, const std::string& user) {
    Frame req; req.opcode = Opcode::DDRemoveUserFromGroup;
    write_lstr16(group, req.payload);
    write_lstr16(user, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDRemoveUserFromGroupAck) {
        return util::Error{5000, 0, "DDRemoveUserFromGroup: server error", user};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_link(
    const std::string& alias, const std::string& path,
    const std::string& user, const std::string& pwd) {
    Frame req; req.opcode = Opcode::DDCreateLink;
    write_lstr16(alias, req.payload);
    write_lstr16(path, req.payload);
    write_lstr16(user, req.payload);
    write_lstr16(pwd, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateLinkAck) {
        return util::Error{5000, 0, "DDCreateLink: server error", alias};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_modify_link(
    const std::string& alias, const std::string& path,
    const std::string& user, const std::string& pwd) {
    Frame req; req.opcode = Opcode::DDModifyLink;
    write_lstr16(alias, req.payload);
    write_lstr16(path, req.payload);
    write_lstr16(user, req.payload);
    write_lstr16(pwd, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDModifyLinkAck) {
        return util::Error{5000, 0, "DDModifyLink: server error", alias};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_ref_integrity(
    const std::string& name, const std::string& failTable,
    const std::string& parent, const std::string& parentTag,
    const std::string& child, const std::string& childTag,
    std::uint16_t updateRule, std::uint16_t deleteRule) {
    Frame req; req.opcode = Opcode::DDCreateRefIntegrity;
    write_lstr16(name, req.payload);
    write_lstr16(failTable, req.payload);
    write_lstr16(parent, req.payload);
    write_lstr16(parentTag, req.payload);
    write_lstr16(child, req.payload);
    write_lstr16(childTag, req.payload);
    write_u16_le(updateRule, req.payload);
    write_u16_le(deleteRule, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateRefIntegrityAck) {
        return util::Error{5000, 0, "DDCreateRefIntegrity: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_create_view(
    const std::string& name, const std::string& comments,
    const std::string& sql) {
    Frame req; req.opcode = Opcode::DDCreateView;
    write_lstr16(name, req.payload);
    write_lstr16(comments, req.payload);
    write_u32_le(static_cast<std::uint32_t>(sql.size()), req.payload);
    req.payload.insert(req.payload.end(), sql.begin(), sql.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDCreateViewAck) {
        return util::Error{5000, 0, "DDCreateView: server error", name};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_add_index_file(
    const std::string& table, const std::string& index,
    const std::string& comment) {
    Frame req; req.opcode = Opcode::DDAddIndexFile;
    write_lstr16(table, req.payload);
    write_lstr16(index, req.payload);
    write_lstr16(comment, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDAddIndexFileAck) {
        return util::Error{5000, 0, "DDAddIndexFile: server error", table};
    }
    return {};
}

util::Result<void> RemoteConnection::dd_remove_index_file(
    const std::string& table, const std::string& index) {
    Frame req; req.opcode = Opcode::DDRemoveIndexFile;
    write_lstr16(table, req.payload);
    write_lstr16(index, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDRemoveIndexFileAck) {
        return util::Error{5000, 0, "DDRemoveIndexFile: server error", table};
    }
    return {};
}

util::Result<std::uint32_t> RemoteConnection::dd_get_permissions(
    const std::string& grantee, std::uint16_t objType,
    const std::string& objName, bool getInherited) {
    Frame req; req.opcode = Opcode::DDGetPermissions;
    write_lstr16(grantee, req.payload);
    write_u16_le(objType, req.payload);
    write_lstr16(objName, req.payload);
    req.payload.push_back(getInherited ? 1 : 0);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDGetPermissionsAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "DDGetPermissions: server error", objName};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<void> RemoteConnection::dd_grant_permission(
    std::uint16_t objType, const std::string& objName,
    const std::string& grantee, std::uint32_t permissions) {
    Frame req; req.opcode = Opcode::DDGrantPermission;
    write_u16_le(objType, req.payload);
    write_lstr16(objName, req.payload);
    write_lstr16(grantee, req.payload);
    write_u32_le(permissions, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DDGrantPermissionAck) {
        return util::Error{5000, 0, "DDGrantPermission: server error", objName};
    }
    return {};
}

util::Result<void> RemoteConnection::set_order(std::uint32_t table_id,
                                                std::uint32_t index_id) {
    Frame req; req.opcode = Opcode::SetOrder;
    write_u32_le(table_id, req.payload);
    write_u32_le(index_id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetOrderAck) {
        std::int32_t code = 5000;
        if (rep.value().payload.size() >= 4)
            code = static_cast<std::int32_t>(
                read_u32_le(rep.value().payload.data()));
        return util::Error{code, 0, "SetOrder: server error", ""};
    }
    return {};
}

util::Result<void>
RemoteConnection::set_order_by_name(std::uint32_t table_id,
                                     const std::string& tag) {
    Frame req; req.opcode = Opcode::SetOrderByName;
    write_u32_le(table_id, req.payload);
    req.payload.insert(req.payload.end(), tag.begin(), tag.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetOrderByNameAck) {
        std::int32_t code = 5000;
        if (rep.value().payload.size() >= 4)
            code = static_cast<std::int32_t>(
                read_u32_le(rep.value().payload.data()));
        return util::Error{code, 0, "SetOrderByName: server error", tag};
    }
    return {};
}

namespace {

// Push a u16-prefixed length + bytes string into a payload buffer.
inline void push_lp_str(std::vector<std::uint8_t>& buf,
                        const std::string& s) {
    auto n = static_cast<std::uint16_t>(s.size());
    buf.push_back(static_cast<std::uint8_t>( n        & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((n >>  8) & 0xFFu));
    buf.insert(buf.end(), s.begin(), s.end());
}

} // namespace

util::Result<std::uint32_t>
RemoteConnection::create_index(std::uint32_t table_id,
                                const std::string& path,
                                const std::string& tag,
                                const std::string& expr,
                                const std::string& cond,
                                const std::string& key_filter,
                                std::uint32_t options,
                                std::uint16_t page_size) {
    Frame req;
    req.opcode = Opcode::CreateIndex;
    write_u32_le(table_id, req.payload);
    write_u32_le(options, req.payload);
    write_u16_le(page_size, req.payload);
    push_lp_str(req.payload, path);
    push_lp_str(req.payload, tag);
    push_lp_str(req.payload, expr);
    push_lp_str(req.payload, cond);
    push_lp_str(req.payload, key_filter);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CreateIndexAck ||
        rep.value().payload.size() < 4) {
        return util::Error{5000, 0, "CreateIndex: server error",
                           tag.empty() ? path : tag};
    }
    return read_u32_le(rep.value().payload.data());
}

util::Result<void>
RemoteConnection::create_table(const std::string& name,
                                const std::string& fields,
                                std::uint16_t table_type,
                                std::uint16_t char_type,
                                std::uint16_t memo_block_size) {
    Frame req;
    req.opcode = Opcode::CreateTable;
    write_u16_le(table_type, req.payload);
    write_u16_le(char_type, req.payload);
    write_u16_le(memo_block_size, req.payload);
    push_lp_str(req.payload, name);
    push_lp_str(req.payload, fields);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::CreateTableAck) {
        return util::Error{5000, 0, "CreateTable: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    return {};
}

util::Result<void>
RemoteConnection::drop_table(const std::string& name,
                              std::uint16_t delete_files) {
    Frame req;
    req.opcode = Opcode::DropTable;
    push_lp_str(req.payload, name);
    write_u16_le(delete_files, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DropTableAck) {
        return util::Error{5000, 0, "DropTable: server error",
                           std::string(rep.value().payload.begin(),
                                       rep.value().payload.end())};
    }
    return {};
}

namespace {

util::Error fs_wire_err(const Frame& rep, const char* op) {
    std::string msg(rep.payload.begin(), rep.payload.end());
    std::int32_t code = 5000;
    if (rep.payload.size() >= 4) {
        code = static_cast<std::int32_t>(read_u32_le(rep.payload.data()));
    }
    return util::Error{code, 0, std::string(op) + ": server error", msg};
}

std::uint64_t read_u64_le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

} // namespace

util::Result<bool>
RemoteConnection::file_exists(const std::string& path) {
    Frame req;
    req.opcode = Opcode::FileExists;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FileExistsAck ||
        rep.value().payload.empty())
        return fs_wire_err(rep.value(), "FileExists");
    return rep.value().payload[0] != 0;
}

util::Result<void>
RemoteConnection::file_erase(const std::string& path) {
    Frame req;
    req.opcode = Opcode::FileErase;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FileEraseAck)
        return fs_wire_err(rep.value(), "FileErase");
    return {};
}

util::Result<void>
RemoteConnection::file_rename(const std::string& old_p,
                              const std::string& new_p) {
    Frame req;
    req.opcode = Opcode::FileRename;
    push_lp_str(req.payload, old_p);
    push_lp_str(req.payload, new_p);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FileRenameAck)
        return fs_wire_err(rep.value(), "FileRename");
    return {};
}

util::Result<std::uint64_t>
RemoteConnection::file_size(const std::string& path) {
    Frame req;
    req.opcode = Opcode::FileSize;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FileSizeAck ||
        rep.value().payload.size() < 8)
        return fs_wire_err(rep.value(), "FileSize");
    return read_u64_le(rep.value().payload.data());
}

util::Result<openads::engine::DirEntry>
RemoteConnection::file_mtime(const std::string& path) {
    Frame req;
    req.opcode = Opcode::FileMTime;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FileMTimeAck ||
        rep.value().payload.size() < 7)
        return fs_wire_err(rep.value(), "FileMTime");
    openads::engine::DirEntry e;
    const auto& pl = rep.value().payload;
    e.year = static_cast<std::uint16_t>(pl[0] | (pl[1] << 8));
    e.mon = pl[2]; e.day = pl[3];
    e.hh = pl[4]; e.mm = pl[5]; e.ss = pl[6];
    return e;
}

util::Result<std::vector<openads::engine::DirEntry>>
RemoteConnection::directory(const std::string& mask) {
    Frame req;
    req.opcode = Opcode::Directory;
    push_lp_str(req.payload, mask);
    write_u16_le(0, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DirectoryAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "Directory");
    std::size_t off = 0;
    std::uint32_t n = read_u32_le(rep.value().payload.data());
    off = 4;
    // Guard against corrupt/huge counts (avoids std::length_error).
    if (n > 100000u)
        return util::Error{5000, 0, "Directory: count too large", mask};
    std::vector<openads::engine::DirEntry> out;
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        openads::engine::DirEntry e;
        if (!openads::engine::unpack_dir_entry(rep.value().payload, off, e))
            return util::Error{5000, 0, "Directory: bad entry", mask};
        out.push_back(std::move(e));
    }
    return out;
}

util::Result<std::vector<std::string>>
RemoteConnection::find_tables(const std::string& mask) {
    Frame req;
    req.opcode = Opcode::FindTables;
    push_lp_str(req.payload, mask);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FindTablesAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FindTables");
    std::size_t off = 0;
    std::uint32_t n = read_u32_le(rep.value().payload.data());
    off = 4;
    if (n > 100000u)
        return util::Error{5000, 0, "FindTables: count too large", mask};
    std::vector<std::string> out;
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::string name;
        if (!read_lstr16(rep.value().payload, off, name))
            return util::Error{5000, 0, "FindTables: bad entry", mask};
        out.push_back(std::move(name));
    }
    return out;
}

util::Result<bool>
RemoteConnection::dir_exist(const std::string& path) {
    Frame req;
    req.opcode = Opcode::DirExist;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DirExistAck ||
        rep.value().payload.empty())
        return fs_wire_err(rep.value(), "DirExist");
    return rep.value().payload[0] != 0;
}

util::Result<void>
RemoteConnection::dir_make(const std::string& path) {
    Frame req;
    req.opcode = Opcode::DirMake;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DirMakeAck)
        return fs_wire_err(rep.value(), "DirMake");
    return {};
}

util::Result<void>
RemoteConnection::dir_remove(const std::string& path) {
    Frame req;
    req.opcode = Opcode::DirRemove;
    push_lp_str(req.payload, path);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::DirRemoveAck)
        return fs_wire_err(rep.value(), "DirRemove");
    return {};
}

util::Result<std::uint32_t>
RemoteConnection::fopen(const std::string& path, std::uint16_t mode) {
    Frame req;
    req.opcode = Opcode::FOpen;
    push_lp_str(req.payload, path);
    write_u16_le(mode, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FOpenAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FOpen");
    return read_u32_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::fcreate(const std::string& path, std::uint16_t attr) {
    Frame req;
    req.opcode = Opcode::FCreate;
    push_lp_str(req.payload, path);
    write_u16_le(attr, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FCreateAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FCreate");
    return read_u32_le(rep.value().payload.data());
}

util::Result<void>
RemoteConnection::fclose(std::uint32_t file_id) {
    Frame req;
    req.opcode = Opcode::FClose;
    write_u32_le(file_id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FCloseAck)
        return fs_wire_err(rep.value(), "FClose");
    return {};
}

util::Result<std::vector<std::uint8_t>>
RemoteConnection::fread(std::uint32_t file_id, std::uint32_t nbytes) {
    Frame req;
    req.opcode = Opcode::FRead;
    write_u32_le(file_id, req.payload);
    write_u32_le(nbytes, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FReadAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FRead");
    std::uint32_t n = read_u32_le(rep.value().payload.data());
    if (4u + n > rep.value().payload.size())
        return util::Error{5000, 0, "FRead: short data", ""};
    return std::vector<std::uint8_t>(
        rep.value().payload.begin() + 4,
        rep.value().payload.begin() + 4 + static_cast<std::ptrdiff_t>(n));
}

util::Result<std::uint32_t>
RemoteConnection::fwrite(std::uint32_t file_id, const std::uint8_t* data,
                         std::uint32_t n) {
    Frame req;
    req.opcode = Opcode::FWrite;
    write_u32_le(file_id, req.payload);
    write_u32_le(n, req.payload);
    if (data && n)
        req.payload.insert(req.payload.end(), data, data + n);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FWriteAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FWrite");
    return read_u32_le(rep.value().payload.data());
}

util::Result<std::uint32_t>
RemoteConnection::fseek(std::uint32_t file_id, std::int32_t offset,
                        std::uint8_t origin) {
    Frame req;
    req.opcode = Opcode::FSeek;
    write_u32_le(file_id, req.payload);
    write_u32_le(static_cast<std::uint32_t>(offset), req.payload);
    req.payload.push_back(origin);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FSeekAck ||
        rep.value().payload.size() < 4)
        return fs_wire_err(rep.value(), "FSeek");
    return read_u32_le(rep.value().payload.data());
}

util::Result<void>
RemoteConnection::skip_unique(std::uint32_t index_id,
                               std::int32_t  direction) {
    Frame req; req.opcode = Opcode::SkipUnique;
    write_u32_le(index_id, req.payload);
    write_u32_le(static_cast<std::uint32_t>(direction), req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SkipUniqueAck) {
        return util::Error{5000, 0, "SkipUnique: server error", ""};
    }
    return {};
}

util::Result<void>
RemoteConnection::set_scope(std::uint32_t index_id,
                             std::uint16_t which,
                             const std::string& key,
                             std::uint16_t data_type) {
    // Payload: u32 index_id | u16 which | u16 data_type | bytes key.
    // Key length is the trailing byte count (payload.size() - 8).
    Frame req; req.opcode = Opcode::SetScope;
    write_u32_le(index_id, req.payload);
    write_u16_le(which, req.payload);
    write_u16_le(data_type, req.payload);
    req.payload.insert(req.payload.end(), key.begin(), key.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::SetScopeAck) {
        return util::Error{5000, 0, "SetScope: server error", key};
    }
    return {};
}

util::Result<RemoteConnection::RowSnapshot>
RemoteConnection::fetch_current_row(std::uint32_t table_id) {
    // Compatibility wrapper for callers that don't carry a
    // RemoteTable: fetch + extract just the visible-row vector. The
    // M12.18 wire format is parsed via the same shared helper as
    // the rt-aware overload below.
    Frame req; req.opcode = Opcode::FetchCurrentRow;
    write_u32_le(table_id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FetchCurrentRowAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0,
            "FetchCurrentRow: server error", ""};
    }
    RemoteTable scratch;
    scratch.conn = this;
    scratch.id   = table_id;
    parse_row_trailer_into(&scratch, rep.value().payload, 0);
    RowSnapshot snap;
    snap.has_row = scratch.row_valid;
    snap.fields  = std::move(scratch.current_row);
    return snap;
}

util::Result<void>
RemoteConnection::fetch_current_row(RemoteTable* rt) {
    Frame req; req.opcode = Opcode::FetchCurrentRow;
    write_u32_le(rt->id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::FetchCurrentRowAck ||
        rep.value().payload.empty()) {
        return util::Error{5000, 0,
            "FetchCurrentRow: server error", ""};
    }
    parse_row_trailer_into(rt, rep.value().payload, 0);
    return {};
}

void RemoteConnection::show_deleted(bool visible) noexcept {
    if (!transport_ || !transport_->valid()) return;
    Frame req;
    req.opcode = Opcode::ShowDeleted;
    req.payload.push_back(visible ? 1 : 0);
    // Best-effort: pre-M12.31 servers lack this opcode; local SET
    // DELETED must still succeed on the client either way.
    (void)request(req);
}

util::Result<void>
RemoteConnection::clear_scope(std::uint32_t index_id,
                               std::uint16_t which) {
    Frame req; req.opcode = Opcode::ClearScope;
    write_u32_le(index_id, req.payload);
    write_u16_le(which, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::ClearScopeAck) {
        return util::Error{5000, 0, "ClearScope: server error", ""};
    }
    return {};
}

util::Result<RemoteConnection::SeekOutcome>
RemoteConnection::seek(std::uint32_t index_id,
                        const std::string& key,
                        std::uint8_t soft,
                        std::uint8_t last,
                        RemoteTable* parent) {
    Frame req;
    req.opcode = last ? Opcode::SeekLast : Opcode::Seek;
    write_u32_le(index_id, req.payload);
    req.payload.push_back(soft);
    req.payload.insert(req.payload.end(), key.begin(), key.end());
    auto rep = request(req);
    if (!rep) return rep.error();
    Opcode want = last ? Opcode::SeekLastAck : Opcode::SeekAck;
    if (rep.value().opcode != want ||
        rep.value().payload.size() < 5) {
        return util::Error{5000, 0, "Seek: server error", key};
    }
    SeekOutcome o;
    o.hit   = rep.value().payload[0];
    o.recno = read_u32_le(rep.value().payload.data() + 1);
    // RCB 07/14/2026: M12.24 — a current-M12.24 server appends the row it
    // landed on after the [u8 found][u32 recno] pair, so the caller does not
    // have to spend a second round-trip fetching it. Strictly optional: an
    // older server sends exactly 5 bytes, we parse no trailer, row_valid stays
    // false, and the old FetchCurrentRow fallback takes over unchanged.
    if (parent != nullptr && rep.value().payload.size() > 5) {
        parse_row_trailer_into(parent, rep.value().payload, 5);
    }
    return o;
}

// M12.35 — query the server for the key expression result type of a
// remote index. Returns ADS_STRING (4), ADS_DATE (3), ADS_NUMERIC (2),
// or ADS_LOGICAL (1) — the same values as the local AdsGetKeyType.

util::Result<std::uint16_t>
RemoteConnection::get_key_type(std::uint32_t index_id) {
    Frame req;
    req.opcode = Opcode::GetKeyType;
    write_u32_le(index_id, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::GetKeyTypeAck ||
        rep.value().payload.size() < 2) {
        return util::Error{5000, 0, "GetKeyType: server error", ""};
    }
    return read_u16_le(rep.value().payload.data());
}

// M12.32 — distributed mutex service.

util::Result<void> RemoteConnection::mutex_create(const std::string& name) {
    Frame req;
    req.opcode = Opcode::Mutex;
    req.payload.push_back(static_cast<std::uint8_t>(MutexOp::Create));
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::Mutex ||
        rep.value().payload.size() < 2 ||
        rep.value().payload[0] != static_cast<std::uint8_t>(MutexOp::Create))
        return util::Error{5000, 0, "MutexCreate: server error", ""};
    if (!rep.value().payload[1])
        return util::Error{5000, 0, "MutexCreate: failed (exists?)", name};
    return util::Result<void>{};
}

util::Result<void> RemoteConnection::mutex_lock(const std::string& name,
                                                std::uint32_t timeout_ms) {
    Frame req;
    req.opcode = Opcode::Mutex;
    req.payload.push_back(static_cast<std::uint8_t>(MutexOp::Lock));
    write_lstr16(name, req.payload);
    write_u32_le(timeout_ms, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::Mutex ||
        rep.value().payload.size() < 2 ||
        rep.value().payload[0] != static_cast<std::uint8_t>(MutexOp::Lock))
        return util::Error{5000, 0, "MutexLock: server error", ""};
    if (!rep.value().payload[1])
        return util::Error{5000, 0, "MutexLock: timeout or not found", name};
    return util::Result<void>{};
}

util::Result<bool> RemoteConnection::mutex_try_lock(const std::string& name) {
    Frame req;
    req.opcode = Opcode::Mutex;
    req.payload.push_back(static_cast<std::uint8_t>(MutexOp::TryLock));
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::Mutex ||
        rep.value().payload.size() < 2 ||
        rep.value().payload[0] != static_cast<std::uint8_t>(MutexOp::TryLock))
        return util::Error{5000, 0, "MutexTryLock: server error", ""};
    return rep.value().payload[1] != 0;
}

util::Result<void> RemoteConnection::mutex_unlock(const std::string& name) {
    Frame req;
    req.opcode = Opcode::Mutex;
    req.payload.push_back(static_cast<std::uint8_t>(MutexOp::Unlock));
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::Mutex ||
        rep.value().payload.size() < 2 ||
        rep.value().payload[0] != static_cast<std::uint8_t>(MutexOp::Unlock))
        return util::Error{5000, 0, "MutexUnlock: server error", ""};
    if (!rep.value().payload[1])
        return util::Error{5000, 0, "MutexUnlock: not owner or not found", name};
    return util::Result<void>{};
}

util::Result<void> RemoteConnection::mutex_destroy(const std::string& name) {
    Frame req;
    req.opcode = Opcode::Mutex;
    req.payload.push_back(static_cast<std::uint8_t>(MutexOp::Destroy));
    write_lstr16(name, req.payload);
    auto rep = request(req);
    if (!rep) return rep.error();
    if (rep.value().opcode != Opcode::Mutex ||
        rep.value().payload.size() < 2 ||
        rep.value().payload[0] != static_cast<std::uint8_t>(MutexOp::Destroy))
        return util::Error{5000, 0, "MutexDestroy: server error", ""};
    if (!rep.value().payload[1])
        return util::Error{5000, 0, "MutexDestroy: not owner or not found", name};
    return util::Result<void>{};
}

} // namespace openads::network
