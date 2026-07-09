---
title: Known issues
layout: default
nav_order: 9
---

# Known issues — current

Status as of **v1.8.2** (2026-07-08).

## Open

### SAP ACE wire protocol

OpenADS speaks its own documented wire protocol on `tcp://` / `tls://`.
It is **not** byte-compatible with the proprietary SAP Advantage 11.x/12.x
TCP protocol. Talking to a legacy ADS server as a drop-in client requires
a future `SapWireTransport` layer (`ads://` / `sap://` URIs).

### CDX rollback to SAP ACE (error 7017)

OpenADS **reads** SAP-built CDX files without modification. Once OpenADS
**writes** a CDX (append, reindex, `INDEX ON`, etc.), the file uses
Harbour's DBFCDX layout (magic `RCHB` at offset 0x14). SAP ACE does not
recognize that header and returns **error 7017** (*Corrupt .ADI, .CDX, or
.IDX index*). Rollback is not automatic — back up original `.cdx` files
before migration, or delete and rebuild all tags under SAP after reverting.
See [Migrating from ADS](en/migrating-from-ads/) for the full checklist.

### OEM national collations beyond PL852

`AdsSetCollation` accepts `BINARY`, `NOCASE`, `NTXPL852`, and `PL852`
(v1.8.0). Other national collation tables from `adslocal.cfg` (e.g.
MAZOVIA, additional NTX* variants) are not implemented yet. Tracked as
[issue #127](https://github.com/FiveTechSoft/OpenADS/issues/127).

### CDX `INDEX ON` write performance

CDX `REINDEX` and `AdsCreateIndex*` now use the bulk bottom-up B+tree
build (v1.8.0) and, for the key-collection phase, direct driver reads
that preserve the read-ahead cache instead of per-record `goto_record`
(which invalidated the cache on every step). This targets the ~11×
gap reported for local rddads `INDEX ON` vs. SAP ACE on real DBFCDX
workloads (issue #128). Further parity work may be needed for
expression evaluation (e.g. UPPER/VAL on OEM data) and multi-tag
resync overheads. Tracked as [issue #128](https://github.com/FiveTechSoft/OpenADS/issues/128).

### SAP-imported Data Dictionary permissions

For `.add` files created by SAP Data Architect, per-table group
permission levels are encoded in encrypted 8-byte blobs that OpenADS
cannot decode yet. Imported DDs may show full DML for every group
where SAP shows read-only access. Use `pmsys_imported.add` (via
`tools/import_dd`) or grant permissions from OpenADS-native tooling.

### TLS certificate verification

`tls://` verifies peer certificates by default. Self-signed or
private-CA endpoints require either a CA bundle (future
`AdsSetTlsCa` entry point) or the dev-only environment variable
`OPENADS_TLS_INSECURE=1`.

### Server-side TLS termination

Client-side TLS (`tls://` in `ace64.dll`) is implemented via mbedtls.
`openads_serverd` does not terminate TLS natively — front it with
nginx, Caddy, or stunnel. See [TLS deployment](en/tls-deployment/).

### Studio LocalServer auth

LocalServer mode (Studio embedded in `ace64.dll` / `ace32.dll`) has no
HTTP Basic auth. The default bind is `127.0.0.1`; if you set
`OPENADS_STUDIO_HOST=0.0.0.0`, put the console behind a reverse proxy
that handles authentication. Remote Server mode (`openads_serverd`)
supports `--http-user user:password`.

### DDL execution

`ALTER TABLE`, `DROP TABLE`, and `DROP INDEX` are parsed but
backend execution hooks are not wired yet.

## Closed recently

- **Linux/macOS CI and release builds broken (NTXPL852 tests)** — fixed
  v1.8.2: shared `polish_oem_fixture.h`; Clang `-Wstring-concatenation` /
  greedy `\x` escape errors in unit tests.
- **`OrdScope` / `AdsSetScope` on character fields** — fixed v1.8.1:
  unpadded scope strings (Harbour `hb_itemGetCLen`) are now space-padded to
  index `key_length` before comparison — work-order `setScopeTop` /
  `setScopeBottom` + `gotop` on local and remote.
- **NTXPL852 / PL852 OEM collation** — fixed v1.8.0: CDX build, seek,
  insert, and reindex honour the PL852 sort table; `AdsSetCollation`
  accepts `NTXPL852` / `PL852`.
- **CDX `REINDEX` per-record rebuild** — improved v1.8.0: bulk
  `build_bulk()` path for CDX tags.
- **Remote `AdsSetScope` / `OrdScope` ignored** — fixed v1.7.0:
  `GotoTop`/`Skip` now honour scoped key ranges over `tcp://`.
- **Remote `OrdKeyCount()` returns 0** — fixed v1.6.5: new `GetKeyCount`
  wire opcode; xBrowse grids show the correct filtered row count.
- **`AdsGetDate()` crash on remote Date fields** — fixed v1.6.5:
  `RemoteIndex` handles resolve to the parent `RemoteTable` before
  field I/O.
- **Numeric `dbSeek` not-found on aliased CDX tags** — fixed v1.6.x:
  seek path converts raw double keys to the stored `FoxNumeric` /
  ASCII form; alias qualifiers stripped on the read side.
- **Conditional index re-create stale order** — fixed v1.6.x:
  in-place `CREATE INDEX` on an open bag rebinds expression, FOR,
  and the active order (`abi_cond_refilter_no_close_test`).
- **WSL2 / Linux `-Werror=conversion` build failures** — fixed:
  narrowing casts and linkage issues in `ace_exports.cpp`; Linux CI
  green on v1.7.0.
- **Remote FiveWin / TDataBase field I/O** — fixed v1.6.4: Date columns,
  ordinal-as-pointer idiom, unlocked-write `EG_UNLOCKED` parity.
- **Remote xBrowse index navigation** — fixed v1.6.3: `AdsKeyNo`,
  bookmark `GotoRecord` sync, `AdsAtBOF` at first key.
- **VFP header 0x32 (autoinc + nullable)** — fixed v1.5.1.
- **Plus SQLite / MSSQL read-only** — fixed v1.5.1: navigational write.
- **Remote `AdsSetRelation` / `AdsSetRecord` / `AdsCustomizeAOF`** —
  fixed v1.5.1 (Fase 2).

See `CHANGELOG.md` for the full per-release breakdown.