---
title: hbnetio Bridge
layout: default
parent: Home (EN)
nav_order: 12
permalink: /en/hbnetio-bridge/
---

# hbnetio Bridge for OpenADS

The hbnetio bridge connects harbour hbnetio virtual file system and
RPC capabilities with OpenADS network transport layer. It enables
transparent remote file access and server-side function execution using
the same hb_vf*() patterns that harbour applications use with hbnetio.

Introduced in v1.8.28.

---

## Components

### VFS Adapter

Maps hbnetio extended virtual file system API to OpenADS ITransport:

| hbnetio | VFS Adapter | Description |
|---------|-------------|-------------|
| hb_vfOpen() | VfsAdapter::open() | Open/create file |
| hb_vfClose() | VfsAdapter::close() | Close file handle |
| hb_vfLock() | VfsAdapter::lock() | Byte-range lock |
| hb_vfUnlock() | VfsAdapter::unlock() | Release lock |
| hb_vfRead() | VfsAdapter::read() | Sequential read |
| hb_vfWrite() | VfsAdapter::write() | Sequential write |
| hb_vfReadAt() | VfsAdapter::read_at() | Positioned read |
| hb_vfWriteAt() | VfsAdapter::write_at() | Positioned write |
| hb_vfSeek() | VfsAdapter::seek() | Set file position |
| hb_vfTrunc() | VfsAdapter::truncate() | Truncate file |
| hb_vfSize() | VfsAdapter::size() | Get file size |
| hb_vfEof() | VfsAdapter::eof() | Test end-of-file |

The adapter implements the exact hbnetio wire protocol (24-byte
little-endian headers, same message IDs), so it can communicate with
any running hbnetio server.

### Distributed Lock Manager

Implements the __isLocked() pattern for multi-client concurrent
access to shared files:

    DistributedLockManager lock_mgr(adapter);
    lock_mgr.set_default_timeout(5000);
    auto guard = lock_mgr.acquire("chats/room1.lck");
    if (guard) {
        // exclusive access
    }
    // guard destructor auto-releases lock

Features:
- Automatic .lck file creation and management
- Configurable retry with backoff (mirrors hb_idleSleep(2))
- Session-scoped lock tracking
- RAII lock guards (never forget to unlock)
- Bulk release on session disconnect
- Heartbeat for long-held locks

### RPC Bridge

Maps hbnetio RPC capabilities to OpenADS session dispatch:

| hbnetio Function | RPC Bridge | Description |
|------------------|-----------|-------------|
| netio_FuncExec() | RpcClient::func_exec() | Call function, get result |
| netio_ProcExec() | RpcClient::proc_exec() | Fire-and-forget procedure |
| netio_ProcExecW() | RpcClient::proc_exec_w() | Wait for completion |
| netio_ProcExists() | RpcClient::proc_exists() | Check availability |
| netio_OpenDataStream() | RpcClient::open_data_stream() | Server push channel |
| netio_GetData() | RpcClient::get_stream_data() | Read stream data |
| netio_CloseStream() | RpcClient::close_stream() | Close channel |

Built-in server functions: server_version, server_time, server_uptime.

---

## Usage Examples

### Remote Chat System

    #include "hbnetio_bridge/vfs_adapter.h"
    #include "hbnetio_bridge/dist_lock_mgr.h"

    using namespace openads::network;

    auto adapter = std::make_unique<VfsAdapter>(std::move(transport));
    adapter->login();

    DistributedLockManager lock_mgr(*adapter);

    // Push a chat note (mirrors PushChatNoteAndPullNotes)
    auto guard = lock_mgr.acquire("chats/room1.lck");
    if (guard) {
        auto handle = adapter->open("chats/room1.txt", FO_READWRITE);
        // read, modify, write
        adapter->close(*handle);
    }

### RPC Function Call

    #include "hbnetio_bridge/rpc_bridge.h"

    // Server side
    RpcBridge bridge;
    bridge.register_func("GetCustomerCount", [](const auto& params) {
        auto table = rpc_serial::unpack_string(params[0]);
        return rpc_serial::pack_u32(count);
    });

    // Client side
    RpcClient client(std::move(transport));
    auto result = client.func_exec("GetCustomerCount",
        {rpc_serial::pack_string("customers")});
    auto count = rpc_serial::unpack_u32(*result);

---

## Configuration

Enable in your OpenADS build:

    cmake -DOPENADS_WITH_HBNETIO_BRIDGE=ON ..

The bridge adds opcodes 0xF0 through 0xFC to the wire protocol. These
do not conflict with OpenADS existing opcodes (which go up to 0xFF
for error).

---

## Thread Safety

All components are thread-safe:
- VfsAdapter: Serializes all wire operations through a mutex
  (matches hbnetio s_fileConLock() pattern)
- DistributedLockManager: Lock registry guarded by mutex
- RpcBridge: Function and stream registries guarded by mutex

---

## Limitations

1. Compression: hbnetio supports ZLIB compression and Blowfish
   encryption. The adapter uses OpenADS TlsTransport for
   encryption instead.
2. Path resolution: Paths are sent as-is; the hbnetio server
   resolves them relative to its root directory.
3. RPC integration: The RPC bridge uses OpenADS native frame
   format (4-byte BE length + opcode + payload), not hbnetio
   24-byte LE header format.

---

## References

- hbnetio source: C:\harbour\contrib\hbnetio\
- OpenADS network: src/network/
- Bridge source: contrib/hbnetio_bridge/
