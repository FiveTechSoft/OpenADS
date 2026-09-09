# OpenADS v1.09.28

## Version reporting: prove which binary answers (client AND server)

Field triage kept stalling on one question — is v1.09.27 actually
what's running on both ends? `AdsVersion()` returns the SAP-shaped
`major.minor+letter` (`1.9a`), which drops the patch and reads the
same for every 1.09.x build, and there was no Harbour-callable way to
ask the *server* for its version at all. This release closes both
gaps with zero wire-protocol changes.

### Changes

- **Hello probe at connect.** `RemoteConnection` sends the ancient
  `Hello` frame before `Connect` and keeps the `HelloAck` payload
  (`"openads/1.09.28"`; the literal `"openads/0.3.2"` on pre-1.8.14
  servers). Best-effort: a failed probe stores empty (unknown) and
  the `Connect` still decides success, so old peers behave exactly as
  before. Cost is one extra RTT per *connect* only — connects are
  rare, USEs are untouched.
- **New `AdsGetServerVersion(hConn, buf, len)`** (OpenADS extension;
  `ace.h` + `.def` + x86 stdcall export). Remote connections report
  the stripped dotted server build; local connections (and
  unresolvable handles) report the DLL's own build. Empty string with
  `AE_SUCCESS` means unknown. A valid local handle never adopts an
  unrelated live remote connection (same guard as `AdsCreateTable`).
- **Harbour bridge** (`tools/fwh_patch/openads_ado_bridge.prg`):
  `OADS_ADSVERSION()` now returns the full dotted build parsed from
  the version description (`"1.09.28"`, SAP-shape fallback), and new
  `OADS_SERVERVERSION(hConn)` exposes the server build to `.prg` code.
  Recommended startup check: `? OADS_ADSVERSION(), OADS_SERVERVERSION(hConn)`.
- **Tests.** New `tests/unit/network_version_test` — remote, local and
  description token agree exactly (same binary in-process) and carry
  the patch.

### Compatibility

- No new opcodes, no changed frame layouts: `Hello`/`HelloAck` date
  back to the earliest wire versions. Mixed old/new peers behave as
  before (unknown version reads as empty).

### Test Results

- MinGW-x64 (GCC 16.2): version suite green; full unit suite green
  except the pre-existing MinGW-only `CdxIndex` alloc-tail case
  (fails identically without these changes; MSVC CI green on v1.09.26/27).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push. Note: the x86 stdcall wrapper and the Harbour bridge `.prg`
  compile only under their native toolchains (MSVC-x86 / Harbour) —
  CI is the verifier for those two files.

### Packages

- `openads-1.09.28-windows-x64.zip`
- `openads-1.09.28-windows-x86.zip`
- `openads-1.09.28-linux-x64.tar.gz`
- `openads-1.09.28-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
