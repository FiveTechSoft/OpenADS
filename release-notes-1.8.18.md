# OpenADS 1.8.18

## Changes

### Added — server filesystem API (`oads_*` / `Ads*`)

Remote (and sandboxed local) file/directory operations under the data
directory, inspired by LetoDB’s `leto_*` helpers:

| Capability | ACE |
|---|---|
| Exists / erase / rename | `AdsCheckExistence`, `AdsDeleteFile`, `AdsRenameFile` |
| Size / time / date | `AdsGetFileSize`, `AdsGetFileTime`, `AdsGetFileDate` |
| List / dirs | `AdsDirectory`, `AdsDirExist`, `AdsDirMake`, `AdsDirRemove` |
| Low-level I/O | `AdsFOpen` / `FCreate` / `FClose` / `FRead` / `FWrite` / `FSeek` |

**Security:** remote ops require `EnableFileFunc=1` in `openads.ini` or
`--enable-file-func` on `openads_serverd` (default **off**). All paths
are jailed under the server data root(s).

**Update both** client `ace32`/`ace64` **and** `openads_serverd`.

See `docs/en/server-filesystem.md` and
`docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`.
