## What's New

### hbnetio Bridge for OpenADS (v1.8.28)

OpenADS can now transparently bridge to harbour's hbnetio virtual file
system and RPC capabilities. This enables multi-client concurrent access
to shared files on a remote hbnetio server (e.g. AWS EC2).

**VFS Adapter** — maps hb_vf*() operations over OpenADS ITransport:

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

**Distributed Lock Manager** — implements the __isLocked() pattern:

- Automatic .lck file creation and management
- Configurable retry with backoff (mirrors hb_idleSleep(2))
- Session-scoped lock tracking with RAII guards
- Bulk release on session disconnect

**RPC Bridge** — maps hbnetio RPC to OpenADS session dispatch:

| hbnetio Function | RPC Bridge | Description |
|------------------|-----------|-------------|
| netio_FuncExec() | RpcClient::func_exec() | Call function, get result |
| netio_ProcExec() | RpcClient::proc_exec() | Fire-and-forget |
| netio_ProcExecW() | RpcClient::proc_exec_w() | Wait for completion |
| netio_ProcExists() | RpcClient::proc_exists() | Check availability |
| netio_OpenDataStream() | RpcClient::open_data_stream() | Server push |
| netio_GetData() | RpcClient::get_stream_data() | Read stream |
| netio_CloseStream() | RpcClient::close_stream() | Close channel |

### Configuration

Enable: `cmake -DOPENADS_WITH_HBNETIO_BRIDGE=ON ..`

Wire opcodes 0xF0-0xFC (no conflict with existing opcodes).

### Files

- `contrib/hbnetio_bridge/vfs_adapter.h/.cpp` - VFS adapter
- `contrib/hbnetio_bridge/dist_lock_mgr.h/.cpp` - Lock manager
- `contrib/hbnetio_bridge/rpc_bridge.h/.cpp` - RPC bridge
- `contrib/hbnetio_bridge/CMakeLists.txt` - Build option
- `contrib/hbnetio_bridge/README.md` - Documentation
- `docs/en/hbnetio-bridge.md` - Reference guide

---

### HB_FUNC Wrappers for Harbour (contrib/oads_hb/)
### HB_FUNC Wrappers for Harbour (contrib/oads_hb/)

Harbour PRG code can now call the oads_*() C functions directly:

| Function | Parameters | Returns |
|---|---|---|
| OADS_FCreate | (hConn, cFile, nAttr) | hFile (0=fail) |
| OADS_FOpen | (hConn, cFile, nMode) | hFile (0=fail) |
| OADS_FClose | (hFile) | lOk |
| OADS_FWrite | (hFile, cData) | nBytesWritten |
| OADS_FRead | (hFile, nLen) | cData |
| OADS_FSeek | (hFile, nOffset, nOrigin) | nPosition |

### Comprehensive Tests

- oads_imac_test — standalone test against iMac LAN server (all 6 functions)
- abi_oads_file_funcs_test — doctest unit tests (local + remote)
- test_oads_file.prg — Harbour PRG test

### Test Results

Tested against iMac at 192.168.18.184:16262 — 34/36 passed.
