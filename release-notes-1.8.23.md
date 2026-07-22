# OpenADS 1.8.23

## Changes

### Fixed — compatibility and remote filesystem/index paths

The Harbour compatibility patch is now structurally valid and passes
`git apply --check`. The ACE DLL exports `oads_F*` aliases in addition to
`AdsF*`, Studio discovers DBFs below `--data` subdirectories, and Windows
fully qualified CDX paths are normalized for remote/POSIX servers.

### Added — TCP connection authentication

Configure repeatable server credentials with `--auth-user client:change-me`
or `auth_user = client:change-me` in `openads.ini`. `AdsConnect60()` forwards
its credentials and invalid or missing credentials return `AE_LOGIN_FAILED`
(`7077`). Use a `tls://host:port/data` URI to encrypt credentials in transit.

### Added — server prints version on launch

`openads_serverd` now shows the build version in the startup banner
(same string as `--version`):

```text
openads_serverd 1.8.23 listening on 0.0.0.0:6262 (backlog=16)
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
| `openads-1.8.23-windows-x64.zip` | `ace64.dll`, `openace64.dll`, `openads_serverd.exe` (HTTP ON), import libs |
| `openads-1.8.23-windows-x86.zip` | `ace32.dll`, `openace32.dll` (stdcall), `openads_serverd.exe` (HTTP ON), import libs |
