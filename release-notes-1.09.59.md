# OpenADS v1.09.59

## Server-side backup archiving: `OAds_Zip` / `OAds_UnZip`

Files stay on the server under `--data`; zips land in
`<root>/backup/<name>_YYYYMMDD.zip`. Built-in engine feature
(works on every leg, no Harbour dependency anywhere).

### Usage

- `OAds_Zip( [hConn,] cDir, aFiles, cZipName [, nLevel [, lOverwrite
  [, cPassword [, aExclude [, lWithPath ]]]]] )`
  → `{ nFiles, nBytes, nArchiveBytes, cArchive }`, NIL on failure.
- `OAds_UnZip( [hConn,] cDir, cZip [, cPassword [, lOverwrite
  [, lWithPath ]]] )` → `{ nFiles, nBytes, nArchiveBytes }`, NIL
  on failure. Bare archive names resolve under `backup/`.
- Server flushes open tables covering the sources first
  (flush-and-go; open tables included, never skipped). Unzip
  refuses targets open on the connection (`AE_FILE_IN_USE`).
  Every path is jailed (`AE_ACCESS_DENIED` on escape); Zip-Slip
  entries rejected; no half-archives left behind on failure.
- ZipCrypto passwords (naked on the wire per deployment call —
  server-local only). Timestamps not restored on extract (v1).

### Changes

- New: vendored zlib 1.3.1 + classic minizip (third_party,
  ZipCrypto read path re-enabled — upstream disables it),
  `engine/zip_arch.*`, `Connection::zip_archive/unzip_archive`,
  `AdsZipFiles`/`AdsUnzipFiles` (+ stdcall + `.def`), wire ops
  `ZipArchive`/`UnzipArchive` (0x17–0x1A, documented in
  wire-protocol.md §5.27), `OADS_ZIP`/`OADS_UNZIP` wrappers.
- Tests: engine round-trips (incl. password, Zip-Slip, excludes,
  collisions), ACE local (dated naming, jail, overwrite,
  flush-and-go, fail-if-open) and remote wire round-trip.

### Test Results

- Local HRB proof build: zip suites 10/10; full unit suite
  1588/1589 (sole failure pre-existing, MinGW-only CDX
  alloc-tail, fails identically on the clean tree).
- New/changed TUs verified clang-strict clean
  (-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.59-windows-x64.zip`
- `openads-1.09.59-windows-x86.zip`
- `openads-1.09.59-linux-x64.tar.gz`
- `openads-1.09.59-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
