# OpenADS 1.8.21

## Changes

### Added — server prints version on launch

`openads_serverd` now shows the build version in the startup banner
(same string as `--version`):

```text
openads_serverd 1.8.21 listening on 0.0.0.0:6262 (backlog=16)
```

The error-log “server started” line also includes the version (useful
when the process runs as a Windows Service with no console).

### Also includes (1.8.18–1.8.20)

- Windows packages ship Studio HTTP (`OPENADS_WITH_HTTP=ON`)
- Harbour rddads DD props 121–130 (`ADS_DD_DISABLE_DLL_CACHING`)
- Server filesystem API (`oads_*` / `Ads*`) with `EnableFileFunc`
- Remote xBrowse scope + SET DELETED OrdKeyCount fix
- Remote `DbCreate` / `AdsCreateTable` over the wire

## Binaries

| Archive | Contents |
|---------|----------|
| `openads-1.8.21-windows-x64.zip` | `ace64.dll`, `openace64.dll`, `openads_serverd.exe` (HTTP ON), import libs |
| `openads-1.8.21-windows-x86.zip` | `ace32.dll`, `openace32.dll` (stdcall), `openads_serverd.exe` (HTTP ON), import libs |
