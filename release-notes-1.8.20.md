# OpenADS 1.8.20

## Changes

### Fixed — Windows packages include Studio HTTP

v1.8.19 Windows binaries were packaged with `OPENADS_WITH_HTTP` off in the
release CMake cache. Starting the service with `--http-port 6263` then
reported that the build lacked HTTP support, so Studio never bound
`http://localhost:6263`.

Release builds now force `-DOPENADS_WITH_HTTP=ON` for x64 and x86.
Example:

```text
openads_serverd --port 6262 --data C:\OpenADS\data --http-port 6263
```

Then open `http://localhost:6263` in a browser.

### Fixed — Harbour rddads: `ADS_DD_DISABLE_DLL_CACHING` undeclared

`include/openads/ace.h` now defines SAP data-dictionary property IDs
121–130, including `ADS_DD_DISABLE_DLL_CACHING` (125). Rebuild rddads
against this header (or this release’s `ace.h`).

### Also includes (1.8.18–1.8.19)

- Server filesystem API (`oads_*` / `Ads*`) with `EnableFileFunc`
- Remote xBrowse scope + SET DELETED OrdKeyCount fix
- Remote `DbCreate` / `AdsCreateTable` over the wire

## Binaries

| Archive | Contents |
|---------|----------|
| `openads-1.8.20-windows-x64.zip` | `ace64.dll`, `openace64.dll`, `openads_serverd.exe` (HTTP ON), import libs |
| `openads-1.8.20-windows-x86.zip` | `ace32.dll`, `openace32.dll` (stdcall), `openads_serverd.exe` (HTTP ON), import libs |
