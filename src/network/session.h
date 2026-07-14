#pragma once

#include "network/frame_reader.h"
#include "network/server.h"
#include "network/socket.h"
#include "network/wire.h"
#include "openads/ace.h"
#include "session/connection.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openads::engine { class Table; }

namespace openads::network {

// Result of dispatching one wire frame. `reply == std::nullopt` means
// "send nothing back" (used by Disconnect); `close_session` asks the
// driving loop to stop reading and tear the connection down.
struct DispatchResult {
    std::optional<Frame> reply;          // std::nullopt => send nothing
    bool                 close_session = false;
};

// SLICE 3a — all per-connection state and the opcode dispatch switch
// extracted verbatim out of Server::session_loop. The existing
// thread-per-connection loop constructs a Session per accepted socket
// and drives it through dispatch(). Zero behavior change.
class Session {
public:
    Session(Server& srv, Socket s);      // computes peer addr, register_session, install_session_socket
    ~Session();                          // cleanup(); srv_->erase_session_socket(sid_); srv_->unregister_session(sid_);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    DispatchResult dispatch(const Frame& f);
    std::uint64_t  id() const noexcept { return sid_; }

    // Read one frame off this connection, dispatch it, write any reply,
    // and fold in the per-frame telemetry. Returns false when the
    // connection should be torn down (peer closed, write failed, or the
    // opcode asked to close). Drives both the legacy thread-per-connection
    // loop and the reactor WorkerPool, so the per-frame contract lives in
    // exactly one place.
    bool handle_readable();

    // Accessor for the reactor: the connection socket this Session owns.
    Socket socket() const noexcept { return s_; }

private:
    // Telemetry + dispatch + reply for one complete frame. Shared by the
    // blocking thread-per-connection loop and the non-blocking reactor path.
    // Returns false when the connection should be torn down.
    bool process_frame(const Frame& f);

    Server*       srv_;
    Socket        s_;
    std::uint64_t sid_;
    // Reassembles complete frames from partial non-blocking reads (reactor
    // path). Harmless on the blocking path — each read yields a whole frame.
    FrameReader   reader_;

    // M12.4 — per-session state. Connection is opened by the
    // Connect frame; OpenTable allocates a session-scoped 32-bit
    // table id keyed into engine handles.
    std::unique_ptr<openads::session::Connection> sess_conn_;
    std::unordered_map<std::uint32_t, openads::session::Handle> tbls_;
    // Original OpenTable payload (DD alias or relative path). ensure_abi_handle
    // must reopen the same physical file — basename-only breaks subdir tables.
    std::unordered_map<std::uint32_t, std::string>            tbl_open_paths_;
    std::unordered_map<std::uint32_t, ADSHANDLE>                cursor_tbls_;
    // M12.16 — lazy-promoted ABI handle parallel to tbls_.
    std::unordered_map<std::uint32_t, ADSHANDLE>                tbls_h_;
    // M12.16 — index_h_[index_id] holds the ABI hIndex.
    std::unordered_map<std::uint32_t, ADSHANDLE>                index_h_;
    // M12.16 — reverse map index_id -> table_id.
    std::unordered_map<std::uint32_t, std::uint32_t>            index_table_;
    // Table ids with an active controlling order. Index ops set the order
    // on the ABI handle (tbls_h_), not the engine table, so when one is
    // active GotoTop / GotoBottom / Skip must navigate the ABI handle and
    // sync the engine cursor — otherwise they walk natural order and the
    // remote browse shows no index.
    std::unordered_set<std::uint32_t>                          ordered_tables_;
    ADSHANDLE     abi_conn_ = 0;
    ADSHANDLE     abi_stmt_ = 0;
    std::uint32_t next_id_ = 1;
    // RCB 06/30/2026: Remote sessions keep DD credentials so lazy server-side
    // ABI handles used by SQL/index operations authenticate the same user.
    std::string   session_user_;
    std::string   session_password_;
    // M12.21 option C — set from the Connect payload's capability word.
    bool          client_prefetch_ok_ = false;
    // M12.32 — last ShowDeleted state received; abi_conn_ is created
    // lazily, so ensure_abi_conn must re-apply it or the ABI connection
    // starts with the default (show) and ordered walks leak deleted rows.
    bool          show_deleted_ = true;

    // Moved helpers (were [&] lambdas in session_loop) -> private
    // methods; bodies unchanged except member renames.
    void      cleanup();
    bool      ensure_abi_conn();
    ADSHANDLE ensure_abi_handle(std::uint32_t id);
    bool      pack_one_row_engine(std::vector<std::uint8_t>& dst,
                                  openads::engine::Table* tbl);
    bool      pack_one_row_abi(std::vector<std::uint8_t>& dst,
                               ADSHANDLE h_abi);
    void      pack_row_trailer(Frame& reply, std::uint32_t id,
                               std::uint16_t lookahead_n = 0);
    void      sync_engine_cursor(std::uint32_t id);
};

} // namespace openads::network
