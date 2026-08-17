## v1.8.87 — resolve audit trail + remote is safe storage (Pritpal Bedi)

### Why

Same-user login from an ADS instance and a DBF instance was failing
because the two sides were not always looking at the same physical
file — or ADS was opening a leftover host-absolute table
(`C:\Creative.RAM\...`) that happened to exist on the box. The
application source still spells those legacy paths; they are hard to
change. We needed an authentic, preservable record of every resolve,
and remote sessions must not touch the host leftover.

### Resolve audit log

```
HYT673 00000001 2026-08-16 22:12:13.826 RESOLVED="C:/temp/creative.ram/CAC00001\USERS.dbf" (sandboxed) asked="C:/Creative.RAM/USERS.dbf" via=local
```

| Field | Width | Meaning |
|-------|-------|---------|
| Connection serial | 6 `[0-9A-Z]` | One id per `Connection::open` |
| Entry serial | 8 decimal | Increments per resolve on that connection |
| Timestamp | `YYYY-MM-DD HH:MM:SS.mmm` | Local wall clock |
| `RESOLVED=` | path or `(remote)` | File ACE actually opened |
| Tag | `(sandboxed)` / `(client)` / `(remote)` | Jail, host leftover, or wire |
| `asked=` | caller name | What the source passed |
| `via=` | `local` / `remote` | ACE connection kind |

- Console default: only `RESOLVED`.
- Detail (`input=`, LEGACY remap/fold, FALLBACK, `OPEN mode=`) is
  optional: `OPENADS_RESOLVE_VERBOSE=1` or `OPENADS_LOG=debug|trace`.
- File (`OPENADS_LOG_FILE=<path>`): **only** `RESOLVED` lines.

How to read a failed ADS+DBF login:

| Line | Meaning |
|------|---------|
| `via=local` `(client)` | ADS opened a host leftover — that is the local table fighting DBF |
| `via=local` `(sandboxed)` | ADS remounted under `--data`; DBF is probably still on the host tree |
| `RESOLVED="(remote)" via=remote` | Client sent the name over the wire; check the **server** log |

### Remote = safe storage

A session owned by `openads_serverd` (and its lazy ABI twin) never
`stat()`s or opens the client's host-absolute spelling. `--legacy-paths`
is the remount bridge under `--data`. Without it, the open fails with
the normal RDD error (Harbour `EG_OPEN` / table not found).

### How to capture

```bat
set OPENADS_LOG_FILE=C:\temp\ads-resolve.log
set OPENADS_RESOLVE_VERBOSE=1
```

Copy `ace64.dll` from this release over the ERP's ACE DLL, then retry
the login and search the log for `USERS` / `USUARIO` / `asked=`.

### Binaries

| File | Contents |
|------|----------|
| `openads-1.8.87-windows-x64.zip` | ace64.dll / openace64.dll + openads_serverd.exe |
| `openads-1.8.87-windows-x86.zip` | ace32.dll / openace32.dll + openads_serverd.exe |
| `openads-1.8.87-linux-x64.tar.gz` | libace64 / libopenace64 + serverd |
| `openads-1.8.87-macos-universal.tar.gz` | libace64 / libopenace64 + serverd (arm64+x86_64) |

**Full Changelog**: https://github.com/FiveTechSoft/OpenADS/compare/v1.8.86...v1.8.87
