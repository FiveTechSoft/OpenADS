# hbnetio Bridge for OpenADS

## Overview

This module bridges harbour's hbnetio virtual file system and RPC capabilities
with OpenADS's network transport layer. It enables OpenADS clients to transparently
access remote files and execute server-side functions using the same patterns that
harbour applications use with hb_vf*() functions.

## Components

### 1. VFS Adapter (fs_adapter.h/.cpp)

Maps hbnetio's extended virtual file system API to OpenADS's ITransport:

`
Harbour App                    OpenADS Server               hbnetio Server
───────────                    ──────────────               ──────────────
hb_vfOpen("net:...")  ──TCP──> VfsAdapter::open()  ──RPC──> netio_Server()
hb_vfLock(h,0,1,...)  ──TCP──> VfsAdapter::lock()  ──RPC──> hb_fileLock()
hb_vfRead(h,buf,n)    ──TCP──> VfsAdapter::read()  ──RPC──> hb_fileRead()
`

**Key features:**
- Full hbnetio wire protocol implementation (24-byte headers)
- File open/close/read/write/seek/truncate
- Byte-range locking (exclusive + shared, blocking + non-blocking)
- Directory operations (exists/make/remove)
- File attributes and timestamps
- Hard/symbolic links

### 2. Distributed Lock Manager (dist_lock_mgr.h/.cpp)

Implements the __isLocked() pattern from your hbnetio code:

`cpp
// The harbour pattern:
// IF __isLocked( cLockFile, @pHandle )
//     // ... critical section ...
//     hb_vfUnlock( pHandle, 0, 1 )
//     hb_vfClose( pHandle )
// ENDIF

// The C++ equivalent:
DistributedLockManager lock_mgr(adapter);
auto guard = lock_mgr.acquire("chats/room1.lck", 5000);
if (guard) {
    // ... exclusive access ...
}
// guard destructor releases lock automatically
`

**Key features:**
- Automatic .lck file creation/management
- Configurable retry with backoff (mirrors hb_idleSleep(2))
- Session-scoped lock tracking
- RAII lock guards (never forget to unlock!)
- Bulk release on session disconnect
- Heartbeat for long-held locks

### 3. RPC Bridge (pc_bridge.h/.cpp)

Maps hbnetio's RPC capabilities to OpenADS's session dispatch:

| hbnetio Function | RPC Bridge Method | Description |
|------------------|-------------------|-------------|
| 
etio_FuncExec() | RpcClient::func_exec() | Call function, get result |
| 
etio_ProcExec() | RpcClient::proc_exec() | Fire-and-forget |
| 
etio_ProcExecW() | RpcClient::proc_exec_w() | Wait for completion |
| 
etio_ProcExists() | RpcClient::proc_exists() | Check availability |
| 
etio_OpenDataStream() | RpcClient::open_data_stream() | Server push channel |
| 
etio_GetData() | RpcClient::get_stream_data() | Read stream data |
| 
etio_CloseStream() | RpcClient::close_stream() | Close channel |

**Built-in functions:**
- server_version — returns OpenADS version string
- server_time — returns current server time
- server_uptime — returns server uptime in seconds

## Usage Examples

### Example 1: Remote Chat System (from your code)

`cpp
#include "hbnetio_bridge/vfs_adapter.h"
#include "hbnetio_bridge/dist_lock_mgr.h"

using namespace openads::network;

// Setup
auto transport = make_plain_transport(sock);
auto adapter = std::make_unique<VfsAdapter>(std::move(transport));
adapter->login();

// Create distributed lock manager
DistributedLockManager lock_mgr(*adapter);
lock_mgr.set_default_timeout(5000);

// Push a chat note (mirrors PushChatNoteAndPullNotes)
util::Result<void> push_note(const std::string& chat_id,
                              const std::string& note_json) {
    std::string lock_path = "chats/" + chat_id + ".lck";
    std::string chat_file = "chats/" + chat_id + ".txt";

    auto guard = lock_mgr.acquire(lock_path);
    if (!guard) return guard.error();

    // Read existing notes
    std::string existing;
    auto handle = adapter->open(chat_file, vfs::FO_READWRITE);
    if (handle) {
        std::vector<char> buf(65536);
        auto nread = adapter->read(*handle, buf.data(), buf.size());
        if (nread) {
            existing.assign(buf.data(), *nread);
        }
    }

    // Add new note (in practice, use JSON library)
    existing += note_json + "\n";

    // Write back
    if (handle) {
        adapter->truncate(*handle, 0);
        adapter->write(*handle, existing.data(), existing.size());
        adapter->commit(*handle);
        adapter->close(*handle);
    }

    return util::Ok();
}
`

### Example 2: RPC Bridge Usage

`cpp
#include "hbnetio_bridge/rpc_bridge.h"

// Server side — register functions
RpcBridge bridge;

bridge.register_func("GetCustomerCount",
    [](const auto& params) -> util::Result<std::vector<uint8_t>> {
        // params[0] = table name
        auto table = rpc_serial::unpack_string(params[0]);
        // ... query database ...
        return rpc_serial::pack_u32(count);
    });

bridge.register_proc("LogAudit",
    [](const auto& params) -> util::Result<void> {
        auto user = rpc_serial::unpack_string(params[0]);
        auto action = rpc_serial::unpack_string(params[1]);
        log_info("Audit: {} performed {}", user, action);
        return util::Ok();
    });

// Client side — call functions
RpcClient client(std::move(transport));

// Call a function
auto count = client.func_exec("GetCustomerCount",
    {rpc_serial::pack_string("customers")});
if (count) {
    auto n = rpc_serial::unpack_u32(*count);
    std::cout << "Customers: " << *n << std::endl;
}

// Fire-and-forget
client.proc_exec("LogAudit",
    {rpc_serial::pack_string("admin"),
     rpc_serial::pack_string("login")});
`

### Example 3: Data Stream (Server Push)

`cpp
// Server: open a stream for real-time updates
bridge.register_func("StreamUpdates",
    [&](const auto& params) -> util::Result<std::vector<uint8_t>> {
        auto stream_id = bridge.open_stream("StreamUpdates", params,
            [](uint32_t id, const auto& data) {
                // Called when data is pushed
                std::cout << "Stream " << id << ": " << data.size()
                          << " bytes" << std::endl;
            });
        return rpc_serial::pack_u32(*stream_id);
    });

// Later, push data to the stream
bridge.stream_send(stream_id, {0x01, 0x02, 0x03});
`

## Integration with OpenADS Build

### CMakeLists.txt changes

`cmake
# In root CMakeLists.txt, after option definitions:
option(OPENADS_WITH_HBNETIO_BRIDGE "Build hbnetio bridge" OFF)

# In src/CMakeLists.txt, inside openads_core:
if(OPENADS_WITH_HBNETIO_BRIDGE)
    target_sources(openads_core PRIVATE
        contrib/hbnetio_bridge/vfs_adapter.cpp
        contrib/hbnetio_bridge/dist_lock_mgr.cpp
        contrib/hbnetio_bridge/rpc_bridge.cpp
    )
    target_compile_definitions(openads_core PUBLIC
        OPENADS_WITH_HBNETIO_BRIDGE=1)
endif()
`

### Wire Protocol Extension

The RPC bridge uses opcodes 0xF0-0xFC that don't conflict with OpenADS's
existing wire protocol (which goes up to 0xFF for error). These are defined
in pc_bridge.h as RpcOpcode.

To fully integrate, add these to wire.h:

`cpp
// RPC Bridge opcodes (contrib/hbnetio_bridge)
RpcCall            = 0xF0,
RpcCallAck         = 0xF1,
// ... etc
`

## Thread Safety

All three components are thread-safe:

- **VfsAdapter**: Serializes all calls through a mutex (matches hbnetio's
  s_fileConLock() pattern)
- **DistributedLockManager**: Lock registry guarded by mutex
- **RpcBridge**: Function registry and stream registry guarded by mutex

## Limitations

1. **Wire format compatibility**: The VFS adapter implements the hbnetio
   wire format but the RPC bridge uses OpenADS's native frame format
   (4-byte BE length + opcode + payload). For full hbnetio compatibility,
   use the VFS adapter which speaks the exact hbnetio wire protocol.

2. **Compression**: hbnetio supports ZLIB compression and Blowfish
   encryption. The adapter doesn't implement these — use OpenADS's
   TlsTransport for encryption.

3. **Path resolution**: The VFS adapter sends paths as-is. On the server,
   hbnetio resolves them relative to its root directory.

## Future Enhancements

- [ ] ZLIB compression support (matches hbnetio's compression levels)
- [ ] Blowfish encryption (key exchange in LOGIN handshake)
- [ ] Thread-per-connection pooling (matches hbnetio's connection sharing)
- [ ] Integration with OpenADS Session dispatch (server-side handler)
- [ ] Automatic VFS path mapping via 
etio_SetPath() equivalent

## References

- hbnetio source: C:\harbour\contrib\hbnetio\
- OpenADS network: src/network/ (wire.h, transport.h, server.h, client.h)
- Your code pattern: PushChatNoteAndPullNotes / __isLocked

## Copyright

2026 OpenADS Contributors. Licensed under the same terms as OpenADS (see LICENSE).
