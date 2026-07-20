# Server filesystem API (`oads_*` / `Ads*`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let clients manage files and directories under the OpenADS server data root via ACE `Ads*` APIs and Harbour `oads_*` wrappers, with `EnableFileFunc` gating and path jail — LetoDB parity without `leto_*` names.

**Architecture:** Shared path-jail + filesystem helpers run in-process (local connection data dir) and inside `openads_serverd` session handlers. Remote clients send new wire opcodes on the existing connection; low-level I/O uses session-local file ids. Harbour `oads_*` is a thin layer over `Ads*`.

**Tech Stack:** C++17, CMake, doctest, existing `openads::network` wire framing, Harbour rddads, `std::filesystem` / iostreams.

**Spec:** `docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`

---

## File Structure

| Path | Responsibility |
|------|----------------|
| `src/platform/fs_sandbox.h` / `.cpp` | Path jail resolve + optional wildcard match for Directory |
| `src/engine/server_fs.h` / `.cpp` | Pure FS ops (exists, erase, rename, size, mtime, dir list, mkdir, rmdir, open/read/write/seek/close) against an absolute jailed path; no network |
| `src/network/wire.h` | New opcodes `0xE0`–`0xF5` |
| `src/network/client.h` / `.cpp` | `RemoteConnection` methods for each op |
| `src/network/server.h` / `.cpp` | `enable_file_func_` flag + getter/setter |
| `src/network/session.cpp` | Opcode handlers + per-session open-file map |
| `src/session/handle_registry.h` | `HandleKind::LocalFile`, `HandleKind::RemoteFile` |
| `src/abi/ace_exports.cpp` | Wire up all `Ads*` (fix CheckExistence/DeleteFile; add rest) |
| `include/openads/ace.h` | Declarations for new entry points |
| `src/openads_ace.def` / `openads_ace_x86.def` | Export new symbols |
| `tools/serverd/config_ini.h` / `.cpp` | `EnableFileFunc` / `enable_file_func` |
| `tools/serverd/main.cpp` | `--enable-file-func` CLI + apply to Server |
| `openads.ini.sample` | Document flag (default 0) |
| `tests/unit/fs_sandbox_test.cpp` | Jail + wildcard |
| `tests/unit/server_fs_test.cpp` | Local FS ops |
| `tests/unit/abi_server_fs_test.cpp` | Ads* local + remote (embedded Server) |
| `examples/harbour/oads_fs/oads_fs.prg` (+ `.ch`, `.hbp`) | Harbour wrappers |
| `examples/harbour/oads_fs/smoke_oads_fs.prg` | Optional live smoke |
| `docs/en/server-filesystem.md` | User docs |
| `CHANGELOG.md` | Release notes |

**Opcode map (locked):**

| Opcode | Value | Ack |
|--------|-------|-----|
| FileExists | `0xE0` | `0xE1` |
| FileErase | `0xE2` | `0xE3` |
| FileRename | `0xE4` | `0xE5` |
| FileSize | `0xE6` | `0xE7` |
| FileMTime | `0xE8` | `0xE9` |
| Directory | `0xEA` | `0xEB` |
| DirExist | `0xEC` | `0xED` |
| DirMake | `0xEE` | `0xEF` |
| DirRemove | `0xF0` | `0xF1` |
| FOpen | `0xF2` | `0xF3` |
| FCreate | `0xF4` | `0xF5` |
| FClose | `0xF6` | `0xF7` |
| FRead | `0xF8` | `0xF9` |
| FWrite | `0xFA` | `0xFB` |
| FSeek | `0xFC` | `0xFD` |

(Leave `0xFE` unused; `0xFF` remains Error.)

**Constants:**

```cpp
// src/engine/server_fs.h
inline constexpr std::uint32_t kMaxSessionFiles = 32;
inline constexpr std::uint32_t kMaxFsIoChunk    = 1024u * 1024u; // 1 MiB
```

**FOpen mode bits (match Harbour FO_* where practical):**

```cpp
constexpr std::uint16_t ADS_FO_READ   = 0x0000;
constexpr std::uint16_t ADS_FO_WRITE  = 0x0001;
constexpr std::uint16_t ADS_FO_READWRITE = 0x0002;
// exclusive is default for write; no share bits in v1
```

**FSeek origins:** `0` begin, `1` current, `2` end.

---

## Phase 1 — Path jail + pure FS engine

### Task 1: `fs_sandbox` path jail helper

**Files:**
- Create: `src/platform/fs_sandbox.h`
- Create: `src/platform/fs_sandbox.cpp`
- Create: `tests/unit/fs_sandbox_test.cpp`
- Modify: `src/CMakeLists.txt` (add `platform/fs_sandbox.cpp` to `openads_core`)
- Modify: `tests/CMakeLists.txt` (add test)

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/fs_sandbox_test.cpp
#include "doctest.h"
#include "platform/fs_sandbox.h"
#include "platform/path.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("fs_sandbox: relative path stays under root") {
    auto root = fs::temp_directory_path() / "oads_fs_jail_ok";
    fs::create_directories(root);
    auto r = openads::platform::resolve_fs_path(root.string(), "sub/a.txt");
    REQUIRE(r.has_value());
    CHECK(r->find(root.string()) == 0 ||
          fs::path(*r).lexically_relative(root).native().find("..") ==
              std::string::npos);
}

TEST_CASE("fs_sandbox: parent escape denied") {
    auto root = fs::temp_directory_path() / "oads_fs_jail_esc";
    fs::create_directories(root);
    auto r = openads::platform::resolve_fs_path(root.string(), "../outside.txt");
    CHECK_FALSE(r.has_value());
}

TEST_CASE("fs_sandbox: wildcard match") {
    CHECK(openads::platform::match_wildcard("foo.dbf", "*.dbf"));
    CHECK_FALSE(openads::platform::match_wildcard("foo.cdx", "*.dbf"));
    CHECK(openads::platform::match_wildcard("ab", "a?"));
}
```

- [ ] **Step 2: Run tests — expect fail (missing symbols)**

```powershell
cmake --build C:\OpenADS\build\default --config Release --target openads_test_fs_sandbox -- /v:m /nologo
```

Expected: link or compile error until implementation lands.

- [ ] **Step 3: Implement**

```cpp
// src/platform/fs_sandbox.h
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace openads::platform {

// Resolve client_path under root (or under any of roots). Returns absolute
// canonical path only if still inside a root. Absolute client paths are
// folded: strip root_name/root_directory and join remainder (1.8.15 style).
std::optional<std::string> resolve_fs_path(const std::string& root,
                                           const std::string& client_path);
std::optional<std::string> resolve_fs_path(
    const std::vector<std::string>& roots, const std::string& client_path);

// Case-sensitive glob: * and ? only (Harbour Directory-like basename match).
bool match_wildcard(const std::string& name, const std::string& pattern);

} // namespace openads::platform
```

Implementation: prefer calling existing `resolve_under_root` /
`resolve_under_any_root` from `path.h` after folding drive-absolute names
the same way `Connection::resolve_table_file` does (strip root_path, keep
relative remainder). If `resolve_under_root` already handles this, wrap it
and document; do not duplicate divergent jail logic.

`match_wildcard`: recursive or iterative `*` / `?` matcher on the filename
component only.

- [ ] **Step 4: Wire CMake and run tests**

Add `platform/fs_sandbox.cpp` to the core library source list in
`src/CMakeLists.txt`. Register `fs_sandbox_test.cpp` in
`tests/CMakeLists.txt` like other unit tests.

```powershell
cmake --build C:\OpenADS\build\default --config Release --target openads_test_fs_sandbox -- /v:m /nologo
ctest --test-dir C:\OpenADS\build\default -C Release -R fs_sandbox --output-on-failure
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/platform/fs_sandbox.h src/platform/fs_sandbox.cpp tests/unit/fs_sandbox_test.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(fs): path jail + wildcard helpers for server filesystem API"
```

---

### Task 2: `server_fs` pure filesystem engine

**Files:**
- Create: `src/engine/server_fs.h`
- Create: `src/engine/server_fs.cpp`
- Create: `tests/unit/server_fs_test.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: API header**

```cpp
// src/engine/server_fs.h
#pragma once
#include "util/result.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace openads::engine {

inline constexpr std::uint32_t kMaxSessionFiles = 32;
inline constexpr std::uint32_t kMaxFsIoChunk    = 1024u * 1024u;

struct DirEntry {
    std::string name;
    std::uint64_t size = 0;
    std::uint16_t year = 0;
    std::uint8_t mon = 0, day = 0, hh = 0, mm = 0, ss = 0;
    std::uint32_t attr = 0; // bit4 = directory (0x10), bit0 = readonly
};

util::Result<bool>          fs_exists(const std::string& abs_path);
util::Result<void>          fs_erase(const std::string& abs_path);
util::Result<void>          fs_rename(const std::string& abs_old,
                                      const std::string& abs_new);
util::Result<std::uint64_t> fs_size(const std::string& abs_path);
util::Result<DirEntry>      fs_mtime_as_entry(const std::string& abs_path);
util::Result<std::vector<DirEntry>> fs_directory(const std::string& abs_dir,
                                                 const std::string& mask,
                                                 std::uint16_t attr_flags);
util::Result<bool>          fs_dir_exist(const std::string& abs_path);
util::Result<void>          fs_dir_make(const std::string& abs_path);
util::Result<void>          fs_dir_remove(const std::string& abs_path);

// Open file for I/O. mode: ADS_FO_READ / WRITE / READWRITE.
// create=true truncates/creates (FCreate).
struct FsFile {
    std::fstream stream;
    std::string  path;
};
util::Result<std::unique_ptr<FsFile>> fs_open(const std::string& abs_path,
                                              std::uint16_t mode,
                                              bool create);

} // namespace openads::engine
```

Use `std::filesystem` for meta ops; map not-found to `AE_NO_FILE_FOUND`
(5018), access issues to `AE_ACCESS_DENIED` (7079), other to
`AE_INTERNAL_ERROR` (5000).

`fs_directory`: if `mask` contains a path separator, split dir + basename
pattern; else list `abs_dir` (caller passes directory part). Attribute
filter: if `attr_flags` has directory bit set only, filter dirs, etc.
Minimal v1: ignore complex attr filtering; return all matching names with
correct `attr` bits filled.

- [ ] **Step 2: Unit test**

```cpp
TEST_CASE("server_fs: write size rename erase") {
    auto dir = fs::temp_directory_path() / "oads_server_fs";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto p = (dir / "t.txt").string();
    {
        auto f = openads::engine::fs_open(p, 0x0002, true);
        REQUIRE(f);
        f.value()->stream.write("hi", 2);
        f.value()->stream.flush();
    }
    auto sz = openads::engine::fs_size(p);
    REQUIRE(sz);
    CHECK(*sz == 2);
    auto p2 = (dir / "u.txt").string();
    REQUIRE(openads::engine::fs_rename(p, p2));
    REQUIRE(openads::engine::fs_erase(p2));
    CHECK_FALSE(*openads::engine::fs_exists(p2));
}
```

- [ ] **Step 3: Implement + green tests + commit**

```bash
git add src/engine/server_fs.h src/engine/server_fs.cpp tests/unit/server_fs_test.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(fs): server_fs pure filesystem operations"
```

---

## Phase 2 — Server config + wire opcodes + session handlers

### Task 3: `EnableFileFunc` config + Server flag

**Files:**
- Modify: `tools/serverd/config_ini.h` — add `has_enable_file_func`, `enable_file_func`
- Modify: `tools/serverd/config_ini.cpp` — parse keys `EnableFileFunc`, `enable_file_func` (0/1/true/false)
- Modify: `tools/serverd/main.cpp` — `Args::enable_file_func`, `--enable-file-func`, help text, `apply_ini`, `srv.set_enable_file_func(...)`
- Modify: `src/network/server.h` — `void set_enable_file_func(bool); bool enable_file_func() const;`
- Modify: `src/network/server.cpp` — store `enable_file_func_{false}`
- Modify: `openads.ini.sample`

- [ ] **Step 1: Parser keys**

In `parse_ini`, add:

```cpp
} else if (key == "enablefilefunc" || key == "enable_file_func") {
    // normalize: strip underscores already if you lower+strip; accept EnableFileFunc
    out.has_enable_file_func = true;
    out.enable_file_func = (val == "1" || val == "true" || val == "yes" || val == "on");
}
```

(Match existing key normalization style in `config_ini.cpp`.)

- [ ] **Step 2: CLI**

```cpp
else if (a == "--enable-file-func") { out.enable_file_func = true; }
else if (a == "--disable-file-func") { out.enable_file_func = false; }
```

Default `false`. After `Server srv`, call
`srv.set_enable_file_func(args.enable_file_func)`.

- [ ] **Step 3: Sample ini**

```ini
# Allow remote oads_*/Ads* filesystem ops under data=. Default 0 (denied).
# WARNING: enables client read/write/delete of files under the data root.
# EnableFileFunc = 0
```

- [ ] **Step 4: Commit**

```bash
git add tools/serverd/config_ini.h tools/serverd/config_ini.cpp tools/serverd/main.cpp src/network/server.h src/network/server.cpp openads.ini.sample
git commit -m "feat(serverd): EnableFileFunc config and CLI flag"
```

---

### Task 4: Wire opcodes

**Files:**
- Modify: `src/network/wire.h` — insert opcodes before `Error = 0xFF`

- [ ] **Step 1: Add enum values + comments** (use the opcode table above). Document payload layouts in comments next to each pair (mirror CreateTable style).

Directory entry wire format:

```text
[u16 nameLen][name bytes]
[u64 size LE]
[u16 year][u8 mon][u8 day][u8 hh][u8 mm][u8 ss]
[u32 attr LE]
```

Directory reply: `[u32 count LE][entry × count]`

FileMTime reply: same calendar fields as entry (no name/size/attr required) — use:
`[u16 year][u8 mon day hh mm ss]`

- [ ] **Step 2: Commit**

```bash
git add src/network/wire.h
git commit -m "feat(wire): filesystem opcodes 0xE0-0xFD"
```

---

### Task 5: Session handlers (meta ops first)

**Files:**
- Modify: `src/network/session.cpp`
- Optionally add helpers in anonymous namespace in session.cpp

- [ ] **Step 1: Session state for files**

In `Session` class (or session.cpp private members):

```cpp
std::unordered_map<std::uint32_t, std::unique_ptr<openads::engine::FsFile>> files_;
std::uint32_t next_file_id_ = 1;
```

On disconnect / session teardown, clear `files_`.

- [ ] **Step 2: Gate helper**

```cpp
bool file_func_ok() const {
    return srv_ && srv_->enable_file_func();
}
// if (!file_func_ok()) { reply = err("file functions disabled", AE_ACCESS_DENIED); break; }
```

- [ ] **Step 3: Resolve helper**

```cpp
// Uses srv_->data_dir_ roots + resolve_fs_path; if not connected yet,
// deny. Prefer connection's data directory string if session has one.
```

If Connect has already jailed the session data dir into a single resolved
path, resolve client relative paths under that session root only.

- [ ] **Step 4: Implement cases for FileExists … DirRemove**

Pattern (FileExists):

```cpp
case Opcode::FileExists: {
    if (!file_func_ok()) { reply = err("file functions disabled", openads::AE_ACCESS_DENIED); break; }
    // parse lp_str path
    auto abs = resolve_client_path(path);
    if (!abs) { reply = err("path denied", openads::AE_ACCESS_DENIED); break; }
    auto ex = openads::engine::fs_exists(*abs);
    if (!ex) { reply = err("exists", (UNSIGNED32)ex.error().code); break; }
    reply.opcode = Opcode::FileExistsAck;
    reply.payload.push_back(*ex ? 1 : 0);
    break;
}
```

FileErase / Rename / Size / MTime / Directory / Dir* similarly.

- [ ] **Step 5: Unit test with embedded Server** (meta only)

In `tests/unit/abi_server_fs_test.cpp` start `Server`, set
`set_enable_file_func(true)`, `set_data_dir(temp)`, connect with
`AdsConnect60`, call `AdsCheckExistence` after Task 7 — for this task, test
via `RemoteConnection` methods once Task 6 lands, or temporarily use
low-level Frame request in the test.

Prefer: complete Task 6 client methods first if tests need them; otherwise
hand-craft frames in the unit test using existing encode/decode helpers.

- [ ] **Step 6: Commit meta handlers**

```bash
git add src/network/session.cpp
git commit -m "feat(server): remote filesystem meta opcodes (EnableFileFunc gated)"
```

---

### Task 6: Session low-level FOpen…FSeek

**Files:**
- Modify: `src/network/session.cpp`

- [ ] **Step 1: FOpen / FCreate**

Parse path + mode; resolve jail; enforce `files_.size() < kMaxSessionFiles`;
call `fs_open`; store under `next_file_id_++`; reply `u32 file_id LE`.

- [ ] **Step 2: FClose**

Erase map entry; close stream.

- [ ] **Step 3: FRead**

Parse `file_id`, `nbytes` (cap at `kMaxFsIoChunk`); read into buffer; reply
`u32 nread` + bytes.

- [ ] **Step 4: FWrite**

Parse `file_id`, `nbytes`, bytes; write; reply `u32 nwritten`.

- [ ] **Step 5: FSeek**

Parse `file_id`, `i64 offset`, `u8 origin`; seek; reply `i64 position LE`.

- [ ] **Step 6: Commit**

```bash
git add src/network/session.cpp
git commit -m "feat(server): remote FOpen/FCreate/FClose/FRead/FWrite/FSeek"
```

---

## Phase 3 — Client RemoteConnection + ACE exports

### Task 7: `RemoteConnection` client methods

**Files:**
- Modify: `src/network/client.h`
- Modify: `src/network/client.cpp`

- [ ] **Step 1: Declarations**

```cpp
util::Result<bool>          file_exists(const std::string& path);
util::Result<void>          file_erase(const std::string& path);
util::Result<void>          file_rename(const std::string& old_p,
                                        const std::string& new_p);
util::Result<std::uint64_t> file_size(const std::string& path);
util::Result<openads::engine::DirEntry> file_mtime(const std::string& path);
util::Result<std::vector<openads::engine::DirEntry>>
                            directory(const std::string& mask,
                                      std::uint16_t attr_flags);
util::Result<bool>          dir_exist(const std::string& path);
util::Result<void>          dir_make(const std::string& path);
util::Result<void>          dir_remove(const std::string& path);
util::Result<std::uint32_t> fopen(const std::string& path, std::uint16_t mode);
util::Result<std::uint32_t> fcreate(const std::string& path, std::uint16_t attr);
util::Result<void>          fclose(std::uint32_t file_id);
util::Result<std::vector<std::uint8_t>>
                            fread(std::uint32_t file_id, std::uint32_t nbytes);
util::Result<std::uint32_t> fwrite(std::uint32_t file_id,
                                   const std::uint8_t* data, std::uint32_t n);
util::Result<std::int64_t>  fseek(std::uint32_t file_id, std::int64_t off,
                                  std::uint8_t origin);
```

- [ ] **Step 2: Implement** each with `Frame req; req.opcode = …; push_lp_str; request();` same pattern as `create_table` in `client.cpp` (~1755).

- [ ] **Step 3: Commit**

```bash
git add src/network/client.h src/network/client.cpp
git commit -m "feat(client): RemoteConnection filesystem methods"
```

---

### Task 8: Handle kinds + ACE meta exports

**Files:**
- Modify: `src/session/handle_registry.h` — add `LocalFile = 27`, `RemoteFile = 28`
- Modify: `include/openads/ace.h` — declare new functions
- Modify: `src/abi/ace_exports.cpp` — implement
- Modify: `src/openads_ace.def`, `src/openads_ace_x86.def`
- Create/extend: `tests/unit/abi_server_fs_test.cpp`

**RemoteFile holder:**

```cpp
struct RemoteFileHandle {
    openads::network::RemoteConnection* conn = nullptr;
    std::uint32_t file_id = 0;
};
// stored in static map or state().remote_files like remote_conns
```

**LocalFile:** wrap `std::unique_ptr<engine::FsFile>` in registry.

- [ ] **Step 1: Fix `AdsCheckExistence` and `AdsDeleteFile`**

```cpp
UNSIGNED32 ENTRYPOINT AdsCheckExistence(ADSHANDLE hConn, UNSIGNED8* pucName,
                                        UNSIGNED16* pbExists) {
    if (!pbExists) return fail(AE_INTERNAL_ERROR, "null out");
    *pbExists = 0;
    if (!pucName) return ok();
    auto name = openads::abi::to_internal(pucName, 0);
    ADSHANDLE h = hConn ? hConn : rddads_default_connection();
    if (auto* rc = get_remote_connection(h)) {
        auto r = rc->file_exists(name);
        if (!r) return fail(r.error());
        *pbExists = *r ? 1 : 0;
        return ok();
    }
    // local: resolve under connection data dir
    auto* c = lookup_connection(h);
    if (!c) return fail(AE_INVALID_CONNECTION_HANDLE, "");
    auto abs = openads::platform::resolve_fs_path(c->data_dir(), name);
    if (!abs) return fail(AE_ACCESS_DENIED, "path");
    auto ex = openads::engine::fs_exists(*abs);
    if (!ex) return fail(ex.error());
    *pbExists = *ex ? 1 : 0;
    return ok();
}
```

Mirror for `AdsDeleteFile` → `file_erase` / `fs_erase`.

Note: confirm `Connection` exposes data directory string; if not, add
`const std::string& data_dir() const` on `session::Connection` / engine
connection.

- [ ] **Step 2: New exports**

```c
UNSIGNED32 ENTRYPOINT AdsRenameFile(ADSHANDLE hConnect, UNSIGNED8* pucOld,
                                    UNSIGNED8* pucNew);
UNSIGNED32 ENTRYPOINT AdsGetFileSize(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                     UNSIGNED32* pulSize);
// If size may exceed 32-bit, also document high dword or use UNSIGNED64 if
// ace.h style allows; for Harbour FSize, 32-bit is OK for v1 with overflow
// clamp or fail AE_INTERNAL_ERROR if > 0xFFFFFFFF.

UNSIGNED32 ENTRYPOINT AdsGetFileTime(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                     UNSIGNED8* pucTime, UNSIGNED16* pusLen);
// writes "hh:mm:ss\0"

UNSIGNED32 ENTRYPOINT AdsGetFileDate(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                     UNSIGNED8* pucDate, UNSIGNED16* pusLen);
// writes "YYYYMMDD\0"

UNSIGNED32 ENTRYPOINT AdsDirectory(
    ADSHANDLE hConnect, UNSIGNED8* pucMask, UNSIGNED16 usAttr,
    UNSIGNED8* pucBuffer, UNSIGNED32* pulBufLen);
// Packed buffer or: first call with null buffer returns required size.
// Simpler v1 for Harbour: AdsDirectoryStart/Next OR return JSON — prefer
// packed entries identical to wire so Harbour can parse in oads_Directory.

// Cleaner for Harbour: iterative API
UNSIGNED32 ENTRYPOINT AdsFindFirstFile(ADSHANDLE hConnect, UNSIGNED8* pucMask,
                                       UNSIGNED16 usAttr, ADSHANDLE* phFind);
UNSIGNED32 ENTRYPOINT AdsFindNextFile(ADSHANDLE hFind, … out entry fields…);
UNSIGNED32 ENTRYPOINT AdsFindCloseFile(ADSHANDLE hFind);
```

**Decision for v1 (locked here):** use **one-shot** `AdsDirectory` that fills a
caller buffer with packed entries (same layout as wire). If buffer too small,
return `AE_INSUFFICIENT_BUFFER` and set `*pulBufLen` to required size.
Harbour `oads_Directory` allocates and retries.

Also:

```c
UNSIGNED32 ENTRYPOINT AdsDirExist(ADSHANDLE, UNSIGNED8* path, UNSIGNED16* pb);
UNSIGNED32 ENTRYPOINT AdsDirMake(ADSHANDLE, UNSIGNED8* path);
UNSIGNED32 ENTRYPOINT AdsDirRemove(ADSHANDLE, UNSIGNED8* path);
```

- [ ] **Step 3: Export .def entries**

Add undecorated names to `openads_ace.def` and correct `@N` stdcall aliases
to `openads_ace_x86.def` (compute stack bytes: pointers 4 on x86, etc.).

- [ ] **Step 4: Tests**

```cpp
TEST_CASE("remote fs: enable off denies") {
    Server srv;
    srv.set_enable_file_func(false);
    srv.set_data_dir(data.string());
    srv.start(...);
    // AdsConnect60...
    UNSIGNED16 ex = 1;
    CHECK(AdsCheckExistence(h, (UNSIGNED8*)"a.txt", &ex) == AE_ACCESS_DENIED);
}

TEST_CASE("remote fs: create write list erase") {
    srv.set_enable_file_func(true);
    // AdsFCreate, AdsFWrite, AdsFClose, AdsGetFileSize, AdsDirectory,
    // AdsRenameFile, AdsDeleteFile — all AE_SUCCESS
    // CHECK file exists on server data dir, not in client cwd
}
```

- [ ] **Step 5: Commit**

```bash
git add include/openads/ace.h src/abi/ace_exports.cpp src/session/handle_registry.h \
  src/openads_ace.def src/openads_ace_x86.def tests/unit/abi_server_fs_test.cpp \
  src/session/connection.h src/session/connection.cpp
git commit -m "feat(ace): sandboxed Ads* filesystem meta API (local+remote)"
```

---

### Task 9: ACE low-level F* exports

**Files:**
- Modify: `include/openads/ace.h`, `ace_exports.cpp`, both `.def` files
- Extend: `tests/unit/abi_server_fs_test.cpp`

```c
UNSIGNED32 ENTRYPOINT AdsFOpen(ADSHANDLE hConnect, UNSIGNED8* pucName,
                               UNSIGNED16 usMode, ADSHANDLE* phFile);
UNSIGNED32 ENTRYPOINT AdsFCreate(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                 UNSIGNED16 usAttr, ADSHANDLE* phFile);
UNSIGNED32 ENTRYPOINT AdsFClose(ADSHANDLE hFile);
UNSIGNED32 ENTRYPOINT AdsFRead(ADSHANDLE hFile, void* pBuf,
                               UNSIGNED32 ulLen, UNSIGNED32* pulRead);
UNSIGNED32 ENTRYPOINT AdsFWrite(ADSHANDLE hFile, const void* pBuf,
                                UNSIGNED32 ulLen, UNSIGNED32* pulWritten);
UNSIGNED32 ENTRYPOINT AdsFSeek(ADSHANDLE hFile, INT32 lOffset,
                               UNSIGNED16 usOrigin, UNSIGNED32* pulPos);
// Note: for files >2GB, document 32-bit seek limit in v1 or use INT64 if
// we add AdsFSeek64 later. Prefer UNSIGNED32 position for ACE familiarity.
```

Remote path: register `RemoteFileHandle` with `HandleKind::RemoteFile`.  
Local path: register `FsFile*` with `HandleKind::LocalFile`.  
`AdsFClose` / read / write / seek switch on kind.

- [ ] **Step 1: Implement + test round-trip 1MB write/read in chunks**
- [ ] **Step 2: Commit**

```bash
git commit -m "feat(ace): AdsFOpen/FCreate/FClose/FRead/FWrite/FSeek"
```

---

## Phase 4 — Harbour `oads_*` + docs + smoke

### Task 10: Harbour wrappers

**Files:**
- Create: `examples/harbour/oads_fs/oads_fs.ch`
- Create: `examples/harbour/oads_fs/oads_fs.prg`
- Create: `examples/harbour/oads_fs/oads_fs.hbp`
- Create: `examples/harbour/oads_fs/README.md`

- [ ] **Step 1: Constants**

```harbour
// oads_fs.ch
#ifndef OADS_FS_CH
#define OADS_FS_CH
#define OADS_FO_READ       0
#define OADS_FO_WRITE      1
#define OADS_FO_READWRITE  2
#define OADS_FS_BEGIN      0
#define OADS_FS_CURRENT    1
#define OADS_FS_END        2
#endif
```

- [ ] **Step 2: Wrappers** (call ACE via existing Harbour `Ads*` if exported in rddads, or use `hb_DynCall` / C glue)

If rddads does not export the new `Ads*` symbols to PRG, add minimal
`HB_FUNC` wrappers either:

- Option A (preferred): extend `contrib` patch file under
  `tools/harbour_patch/oads_fs_funcs.c` with `HB_FUNC( OADS_FILE )` etc.
  calling the ACE APIs, OR
- Option B: put `oads_fs_c.c` in `examples/harbour/oads_fs/` and list it in
  the `.hbp` so apps link the C bridge + ace import lib.

Use **Option B** so stock Harbour trees need no patch for this feature.

```c
// examples/harbour/oads_fs/oads_fs_c.c
#include "hbapi.h"
#include "hbapiitm.h"
#include "ace.h"  // from OpenADS include or HB_WITH_ADS

HB_FUNC( OADS_FILE )
{
   UNSIGNED16 ex = 0;
   ADSHANDLE h = (ADSHANDLE) hb_parnint( 2 ); /* optional connect */
   if( AdsCheckExistence( h, (UNSIGNED8*)hb_parc(1), &ex ) == AE_SUCCESS )
      hb_retl( ex != 0 );
   else
      hb_retl( HB_FALSE );
}
/* … OADS_FERASE, OADS_FRENAME, OADS_FSIZE, OADS_FTIME, OADS_FDATE,
     OADS_DIRECTORY, OADS_DIREXIST, OADS_DIRMAKE, OADS_DIRREMOVE,
     OADS_FOPEN, OADS_FCREATE, OADS_FCLOSE, OADS_FREAD, OADS_FWRITE,
     OADS_FSEEK … */
```

`OADS_DIRECTORY`: call `AdsDirectory`, parse packed buffer into Harbour
array of `{ name, size, date, time, attr }` matching `Directory()` layout
where possible (`{ cName, nSize, dDate, cTime, cAttr }`).

- [ ] **Step 3: Thin `.prg` optional** — can be pure C `HB_FUNC`s only; if PRG
  used, just `REQUEST` symbols.

- [ ] **Step 4: README with server config and example**

```harbour
AdsConnect60( "tcp://192.168.18.184:16262//tmp/openads_mac", 2, NIL, NIL, 0, @h )
? oads_DirMake( "inbox" )
n := oads_FCreate( "inbox/hello.txt" )
oads_FWrite( n, "hello" )
oads_FClose( n )
? oads_File( "inbox/hello.txt" )
? oads_FSize( "inbox/hello.txt" )
a := oads_Directory( "inbox/*.*" )
oads_FErase( "inbox/hello.txt" )
oads_DirRemove( "inbox" )
```

- [ ] **Step 5: Commit**

```bash
git add examples/harbour/oads_fs
git commit -m "feat(harbour): oads_* server filesystem wrappers"
```

---

### Task 11: Docs + CHANGELOG

**Files:**
- Create: `docs/en/server-filesystem.md`
- Modify: `docs/en/index.md` (nav link)
- Modify: `CHANGELOG.md`
- Modify: `docs/en/api-reference/index.md` (table rows for new Ads*)

- [ ] **Step 1: User doc** — security warning, EnableFileFunc, path jail,
  function tables (oads_* and Ads*), upgrade note (client+server).

- [ ] **Step 2: CHANGELOG under Unreleased / next version**

```markdown
### Added — server filesystem API (`oads_*` / `Ads*`)
Remote and sandboxed-local file/directory ops gated by EnableFileFunc…
```

- [ ] **Step 3: Commit**

```bash
git commit -m "docs: server filesystem API (oads_*)"
```

---

### Task 12: Live smoke vs iMac (manual / optional CI)

**Files:**
- Create: `tests/smoke/harbour/oads_fs_smoke.prg` or C++ smoke next to
  `build/tmp_remote_create`

- [ ] **Step 1: C++ smoke** (faster than Harbour in this repo)

Reuse pattern of `remote_create_smoke.cpp`:

1. Connect `tcp://192.168.18.184:16262//tmp/openads_mac`
2. Expect ACCESS_DENIED if server has EnableFileFunc off — then document
   restart:  
   `openads_serverd --port 16262 --data /tmp/openads_mac --enable-file-func`
3. DirMake `oads_fs_test` → FCreate file → write → size → directory → erase → DirRemove
4. Assert no files in client cwd

- [ ] **Step 2: Run against iMac** (operator must enable flag once)

```powershell
# Windows client
$env:OPENADS_SMOKE_URI = "tcp://192.168.18.184:16262//tmp/openads_mac"
.\oads_fs_smoke.exe
```

- [ ] **Step 3: Commit smoke source**

```bash
git commit -m "test(smoke): oads filesystem remote smoke client"
```

---

## Spec coverage checklist

| Spec requirement | Task |
|------------------|------|
| oads_File / FErase / FRename / FSize / FTime / FDate | 8, 10 |
| oads_Directory | 8, 10 |
| oads_DirExist / DirMake / DirRemove | 8, 10 |
| oads_FOpen…FSeek | 9, 10 |
| Ads* C API | 8, 9 |
| EnableFileFunc default off | 3, 5 |
| Path jail | 1, 5, 8 |
| Wire opcodes | 4–7 |
| Local + remote same Ads* | 8, 9 |
| Max handles / chunk size | 2, 6 |
| Handles closed on disconnect | 5 |
| openads.ini.sample | 3 |
| Tests unit + remote | 1, 2, 8, 9 |
| Harbour packaging | 10 |
| Docs + CHANGELOG | 11 |
| Live iMac smoke | 12 |
| Fix raw AdsCheckExistence/DeleteFile | 8 |

---

## Execution notes for agents

- Prefer TDD order inside each task: test → fail → implement → pass → commit.
- Do not change unrelated ACE behaviour.
- Rebuild both `openads_ace` and `openads_serverd` after wire changes.
- x86 `.def` `@N` values must match actual stdcall stack size or Harbour
  32-bit will crash on call.
- When unsure of `Connection::data_dir()`, grep `class Connection` in
  `src/session/connection.h` and add a getter rather than peeking private
  members from ABI.
- Author: follow repo convention for commits (`feat(fs):` / `feat(ace):`).

---

## Self-review (plan author)

- No TBD placeholders remain for required behaviour.
- Opcode map locked; payload layouts specified.
- EnableFileFunc default and path folding aligned with design doc.
- Harbour path does not require patching stock rddads (Option B C bridge).
