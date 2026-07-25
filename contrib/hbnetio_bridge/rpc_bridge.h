#pragma once

// ============================================================================
// RPC Bridge — hbnetio <-> OpenADS Server
// ============================================================================
//
// Maps harbour's hbnetio RPC (Remote Procedure Call) capabilities to
// OpenADS's Session/Server architecture. This enables:
//
//   1. Server-side function execution from remote clients
//   2. Bidirectional data streaming
//   3. Transparent RPC over existing OpenADS wire protocol
//
// hbnetio RPC functions:
//   netio_ProcExec()    — fire-and-forget procedure call
//   netio_ProcExecW()   — procedure call, wait for completion
//   netio_FuncExec()    — function call, return value
//   netio_ProcExists()  — check if function exists on server
//   netio_OpenDataStream()  — open async data stream server->client
//   netio_OpenItemStream()  — open async item stream server->client
//   netio_CloseStream()     — close a stream
//   netio_GetData()         — read data from a stream
//
// Architecture:
//
//   Harbour App              OpenADS Session           OpenADS Engine
//   ───────────              ──────────────           ──────────────
//   netio_FuncExec()  ──TCP──> RpcBridge::call()  ──> session dispatch
//   netio_ProcExec()  ──TCP──> RpcBridge::proc()  ──> session dispatch
//   netio_OpenStream()──TCP──> RpcBridge::stream() ──> async channel
//
// Wire protocol extension:
//
//   New opcodes in OpenADS wire.h:
//     RpcCall         = 0xF0   (client -> server: call a function)
//     RpcCallAck      = 0xF1   (server -> client: return value)
//     RpcProc         = 0xF2   (client -> server: fire-and-forget proc)
//     RpcProcAck      = 0xF3   (server -> client: ack/reject)
//     RpcProcW        = 0xF4   (client -> server: proc, wait for ack)
//     RpcProcWAck     = 0xF5   (server -> client: completion ack)
//     RpcProcExists   = 0xF6   (client -> server: check existence)
//     RpcProcExistsAck= 0xF7   (server -> client: true/false)
//     RpcStreamOpen   = 0xF8   (client -> server: open stream)
//     RpcStreamOpenAck= 0xF9   (server -> client: stream id)
//     RpcStreamData   = 0xFA   (server -> client: stream data)
//     RpcStreamClose  = 0xFB   (bidirectional: close stream)
//     RpcStreamCloseAck=0xFC   (acknowledge close)
//
// Copyright 2026 OpenADS Contributors
// Licensed under the same terms as OpenADS (see LICENSE)
// ============================================================================

#include "network/transport.h"
#include "network/wire.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openads::network {

// ---------------------------------------------------------------------------
// Extended opcodes for RPC bridge (must not collide with wire.h Opcode)
// ---------------------------------------------------------------------------
enum class RpcOpcode : std::uint8_t {
    // Function/Procedure execution
    Call            = 0xF0,  // client -> server: execute function
    CallAck         = 0xF1,  // server -> client: return value
    Proc            = 0xF2,  // client -> server: fire-and-forget
    ProcAck         = 0xF3,  // server -> client: ack (or reject)
    ProcW           = 0xF4,  // client -> server: proc, wait
    ProcWAck        = 0xF5,  // server -> client: completion
    ProcExists      = 0xF6,  // client -> server: check existence
    ProcExistsAck   = 0xF7,  // server -> client: true/false

    // Streaming
    StreamOpen      = 0xF8,  // client -> server: open stream
    StreamOpenAck   = 0xF9,  // server -> client: stream id
    StreamData      = 0xFA,  // server -> client: data chunk
    StreamClose     = 0xFB,  // either direction: close
    StreamCloseAck  = 0xFC,  // acknowledge close
};


// ---------------------------------------------------------------------------
// RPC function signature
// ---------------------------------------------------------------------------
//
// The server-side function receives serialized parameters and returns
// a serialized result. Parameters are Harbour-serialized items.
//
// Using hbnetio's convention:
//   netio_FuncExec("myFunc", param1, param2, ...)
//   -> server deserializes params, calls function, serializes result
//
using RpcFunc = std::function<
    util::Result<std::vector<std::uint8_t>>(
        const std::vector<std::vector<std::uint8_t>>& params)>;

using RpcProc = std::function<
    util::Result<void>(
        const std::vector<std::vector<std::uint8_t>>& params)>;

// Stream callback: server pushes data to client
using StreamCallback = std::function<
    void(std::uint32_t stream_id,
         const std::vector<std::uint8_t>& data)>;


// ---------------------------------------------------------------------------
// StreamInfo — tracks an open data stream
// ---------------------------------------------------------------------------
struct StreamInfo {
    std::uint32_t       id = 0;
    std::string         func_name;
    bool                active = false;
    StreamCallback      callback;
    std::thread         sender_thread;
};


// ---------------------------------------------------------------------------
// RpcBridge — server-side RPC handler
// ---------------------------------------------------------------------------
//
// Registers functions that remote clients can call via the RPC bridge.
// Integrates with OpenADS's Session dispatch: incoming RPC frames are
// routed through the bridge, which executes the registered function
// and sends the result back.
//
// Usage (server setup):
//   RpcBridge bridge;
//   bridge.register_func("GetServerTime", [](auto& params) {
//       auto now = std::chrono::system_clock::now();
//       return serialize(now);
//   });
//   bridge.register_proc("LogMessage", [](auto& params) {
//       log_info("Remote: {}", deserialize_string(params[0]));
//       return util::Ok();
//   });
//
//   // In session dispatch:
//   if (frame.opcode == Opcode::RpcCall) {
//       auto result = bridge.handle_call(session_conn, frame);
//       // send result back
//   }
//
class RpcBridge {
public:
    RpcBridge();
    ~RpcBridge();

    RpcBridge(const RpcBridge&) = delete;
    RpcBridge& operator=(const RpcBridge&) = delete;

    // -- Function registration ------------------------------------------------

    // Register a callable function (netio_FuncExec target).
    // The function receives serialized parameters and must return
    // a serialized result (or error).
    void register_func(const std::string& name, RpcFunc func);

    // Register a fire-and-forget procedure (netio_ProcExec target).
    void register_proc(const std::string& name, RpcProc proc);

    // Remove a registration.
    void unregister(const std::string& name);

    // Check if a function is registered (netio_ProcExists target).
    bool func_exists(const std::string& name) const;

    // -- RPC dispatch ---------------------------------------------------------

    // Handle an incoming RpcCall frame. Returns serialized result.
    util::Result<std::vector<std::uint8_t>>
    handle_call(const std::string& func_name,
                const std::vector<std::vector<std::uint8_t>>& params);

    // Handle an incoming RpcProc frame.
    util::Result<void>
    handle_proc(const std::string& proc_name,
                const std::vector<std::vector<std::uint8_t>>& params);

    // Handle an incoming RpcProcW frame (waits for completion ack).
    util::Result<void>
    handle_proc_w(const std::string& proc_name,
                  const std::vector<std::vector<std::uint8_t>>& params);

    // -- Stream management ----------------------------------------------------

    // Open a data stream (server -> client push channel).
    // Calls the named function on the server side, which receives
    // the transport and stream ID. If the function returns the
    // stream ID, the stream is considered open.
    util::Result<std::uint32_t>
    open_stream(const std::string& func_name,
                const std::vector<std::vector<std::uint8_t>>& params,
                StreamCallback callback);

    // Send data through an open stream.
    util::Result<void> stream_send(std::uint32_t stream_id,
                                   const std::vector<std::uint8_t>& data);

    // Close a stream.
    void close_stream(std::uint32_t stream_id);

    // -- Built-in functions ---------------------------------------------------

    // These are automatically registered and provide basic server info.
    static util::Result<std::vector<std::uint8_t>>
    builtin_server_version(const std::vector<std::vector<std::uint8_t>>&);

    static util::Result<std::vector<std::uint8_t>>
    builtin_server_time(const std::vector<std::vector<std::uint8_t>>&);

    static util::Result<std::vector<std::uint8_t>>
    builtin_server_uptime(const std::vector<std::vector<std::uint8_t>>&);

private:
    // Generate a unique stream ID.
    std::uint32_t next_stream_id();

    mutable std::mutex mu_;

    std::unordered_map<std::string, RpcFunc>  funcs_;
    std::unordered_map<std::string, RpcProc>  procs_;
    std::unordered_map<std::uint32_t, std::unique_ptr<StreamInfo>> streams_;
    std::uint32_t stream_seq_ = 1;

    std::chrono::steady_clock::time_point start_time_;
};


// ---------------------------------------------------------------------------
// RpcClient — client-side RPC caller
// ---------------------------------------------------------------------------
//
// Wraps an ITransport and provides methods matching hbnetio's client API.
// Serialises function calls into RPC wire frames and deserialises results.
//
// Usage:
//   RpcClient client(std::move(transport));
//   auto result = client.func_exec("GetServerTime", param1, param2);
//   auto value = deserialize(result.value());
//
class RpcClient {
public:
    explicit RpcClient(std::unique_ptr<ITransport> transport);
    ~RpcClient();

    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    // -- RPC operations -------------------------------------------------------

    // Execute a function on the server and return its result.
    // Equivalent to netio_FuncExec().
    util::Result<std::vector<std::uint8_t>>
    func_exec(const std::string& name,
              const std::vector<std::vector<std::uint8_t>>& params = {});

    // Execute a fire-and-forget procedure.
    // Equivalent to netio_ProcExec().
    util::Result<void>
    proc_exec(const std::string& name,
              const std::vector<std::vector<std::uint8_t>>& params = {});

    // Execute a procedure and wait for completion acknowledgment.
    // Equivalent to netio_ProcExecW().
    util::Result<void>
    proc_exec_w(const std::string& name,
                const std::vector<std::vector<std::uint8_t>>& params = {});

    // Check if a function exists on the server.
    // Equivalent to netio_ProcExists().
    util::Result<bool>
    proc_exists(const std::string& name);

    // -- Stream operations ----------------------------------------------------

    // Open a data stream from server.
    // Equivalent to netio_OpenDataStream().
    util::Result<std::uint32_t>
    open_data_stream(const std::string& func_name,
                     const std::vector<std::vector<std::uint8_t>>& params = {});

    // Open an item stream from server.
    // Equivalent to netio_OpenItemStream().
    util::Result<std::uint32_t>
    open_item_stream(const std::string& func_name,
                     const std::vector<std::vector<std::uint8_t>>& params = {});

    // Read data from a stream.
    // Equivalent to netio_GetData().
    util::Result<std::vector<std::uint8_t>>
    get_stream_data(std::uint32_t stream_id);

    // Close a stream.
    // Equivalent to netio_CloseStream().
    util::Result<void>
    close_stream(std::uint32_t stream_id);

    bool connected() const noexcept { return transport_ && transport_->valid(); }

private:
    // Send a frame and receive a reply.
    util::Result<std::vector<std::uint8_t>>
    send_and_recv(std::uint8_t opcode, const void* data, std::size_t len);

    std::unique_ptr<ITransport> transport_;
    mutable std::mutex mu_;
};


// ---------------------------------------------------------------------------
// Serialization helpers (Harbour hb_itemSerialize compatible)
// ---------------------------------------------------------------------------
//
// These helpers format/parse parameter lists in the same wire format
// that hbnetio uses for RPC. Each parameter is length-prefixed.
//
namespace rpc_serial {

// Pack a string parameter.
std::vector<std::uint8_t> pack_string(const std::string& s);

// Pack raw bytes.
std::vector<std::uint8_t> pack_raw(const void* data, std::size_t len);

// Pack a uint32 value (little-endian).
std::vector<std::uint8_t> pack_u32(std::uint32_t v);

// Pack a uint64 value (little-endian).
std::vector<std::uint8_t> pack_u64(std::uint64_t v);

// Unpack a string from serialized data.
util::Result<std::string> unpack_string(const std::vector<std::uint8_t>& data);

// Unpack raw bytes.
util::Result<std::vector<std::uint8_t>>
unpack_raw(const std::vector<std::uint8_t>& data);

// Unpack a uint32.
util::Result<std::uint32_t> unpack_u32(const std::vector<std::uint8_t>& data);

// Unpack a uint64.
util::Result<std::uint64_t> unpack_u64(const std::vector<std::uint8_t>& data);

// Pack multiple parameters into a single buffer (length-prefixed).
std::vector<std::uint8_t>
pack_params(const std::vector<std::vector<std::uint8_t>>& params);

// Unpack multiple parameters from a buffer.
util::Result<std::vector<std::vector<std::uint8_t>>>
unpack_params(const std::vector<std::uint8_t>& data);

} // namespace rpc_serial

} // namespace openads::network
