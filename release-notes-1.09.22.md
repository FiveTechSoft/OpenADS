# OpenADS v1.09.22

## Fixed — CDX write-lock registry + platform lock improvements for 700-storm

### Changes

- **`clear_cdx_write_lock()`**: New function to release the process-wide
  write-lock registry by path, allowing the ABI handle to acquire the
  byte lock without conflict. Called when releasing engine table's
  CdxIndex so the ABI handle can acquire fcntl(F_WRLCK) for writes.

- **`canonicalize_path()` with `weakly_canonical()`**: Resolves symlinks
  (e.g. `/tmp` → `/private/tmp` on macOS) so that two paths to the same
  file share the same registry entry. Prevents duplicate lock entries.

- **`release_lock_shared()`**: New virtual method in IIndex/CdxIndex for
  POSIX `flock(LOCK_SH)` release before `fcntl(F_WRLCK)` acquisition.
  On macOS, flock and fcntl on the same file interact and a held LOCK_SH
  blocks an exclusive fcntl. No-op on Win32.

- **Platform `File::release_lock_shared()`**: New method to release
  advisory shared lock. On POSIX, calls `flock(fd, LOCK_UN)`. No-op on
  Win32 (flock is a no-op).

- **Diagnostic logging**: Added logging to `ensure_write_lock_()` and
  `open_named()` for debugging lock contention issues.

- **`lock_posix.cpp`**: Capture `os_error` before returning for better
  diagnostics.

### Test Results

- `openads_remote_stress`: 700 clients, 30s duration
  - 1403 full scans (46.8/s)
  - 1,403,000 rows read (46,767/s)
  - Scan latency: p50=1136ms, p95=3338ms, p99=5860ms
  - Peak sessions: 232
  - Errors: 0
  - Miscount: 0

- `openads_remote_stress`: 1000 clients, 60s duration
  - 2779 full scans (46.3/s)
  - 2,779,000 rows read (46,317/s)
  - Scan latency: p50=1281ms, p95=4748ms, p99=9334ms
  - Peak sessions: 275
  - Errors: 0
  - Miscount: 0

- `abi_remote_create_stress_test`: 700 workers
  - 56,044 assertions, 0 failures
  - All DBF+CDX integrity checks passed

### Known Issues

- The `reload_header_if_changed_()` change (deferring refresh when
  `e->users > 0` regardless of owner) was reverted as it caused CDX
  index staleness. The original behavior (only defer when another thread
  owns the batch) is preserved.

### Packages

- `openads-1.09.22-windows-x64.zip`
- `openads-1.09.22-windows-x86.zip`
