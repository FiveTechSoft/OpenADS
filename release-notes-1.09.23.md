# OpenADS v1.09.23

## Fixed — `.z01` production bags (Vouch) + fixed-width text error log

### Changes

- **`.z01` production-bag support (Vouch/ERP).** Vouch keeps the
  CDX-format production index as `<base>.z01` instead of
  `<base>.cdx`. The server only STATed `<base>.cdx`, so every `USE`
  fell back to a speculative `<base>.cdx` `AdsOpenIndex` that always
  failed with `5018 AE_NO_FILE_FOUND` — one wasted round-trip plus a
  synchronous `ads_err.dbf` row per open, with broken index orders.
  The server `OpenTableAck` now STATs `.cdx` then `.z01`
  (`src/network/session.cpp`), the remote fallback tries `.cdx`
  then `.z01`, and the local production auto-open (server ABI twin
  included) binds whichever exists (`src/abi/ace_exports.cpp`).
  Until the new binaries are deployed, symlinking each `.z01` to
  `.cdx` stops the `5018` flood on the old builds.

- **Fixed-width `ads_err.log` text mirror.** Every error-log entry
  is now also appended to `ads_err.log` in the same directory: all
  leading columns fixed-width, exactly 3 spaces apart, one line per
  entry, header written on creation. New developer columns the frozen
  DBF schema cannot carry: `PID`, `TID` (worker-thread hash for MT
  storms), `SESSION` (server sid), `CLIENT` (`ip:port`), `OP` (wire
  op/API), `TABLE` (basename). `DETAIL` is the uncapped tail column
  (multi-line messages flattened with ` | `). Rotation mirrors the
  DBF (drop oldest third) under the same `error_log_max` cap.
  `Session::process_frame` stamps per-frame context; `Op: path`
  details backfill `OP`/`TABLE` for older call sites
  (`src/mgmt/error_log.*`, `src/network/session.*`).
  The `5018 OpenIndex: xxx.cdx` flood that used to need DBF decoding
  is now `grep OpenIndex ads_err.log`.

- **Linux build fix.** Missing POSIX headers (`fcntl.h`,
  `sys/file.h`) in `cdx_index.cpp` broke the Linux build.

- **Stress harness.** 700-worker storm cap raised to 800 with
  `set_max_sessions` for the in-process server (test-only).

### Test Results

- `error_log_test`: new `ads_err.log` case (fixed widths, 3-space
  separators, context columns, DBF round-trip untouched) alongside
  the existing DBF round-trip / size-cap / engine-open cases.
- Full `ctest` suite runs on release CI (Windows x64/x86,
  Linux, macOS) before publish.

### Packages

- `openads-1.09.23-windows-x64.zip`
- `openads-1.09.23-windows-x86.zip`
- `openads-1.09.23-linux-x64.tar.gz`
- `openads-1.09.23-macos-universal.tar.gz`
