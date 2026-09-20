# OpenADS v1.09.67

## Zip listing: `OAds_ZipFileCount` / `OAds_ZipFileList` (hbZipArc parity)

Server-side archives can now be inspected without extracting,
mirroring Harbour's `hb_GetFileCount()` / `hb_GetFilesInZip()`:

```
OAds_ZipFileCount( [hConn,] cZip ) -> nFiles (0 on error)
OAds_ZipFileList( [hConn,] cZip [, lVerbose ] ) -> aFiles ({} on error)
```

- Plain form returns file names; verbose returns hb-ordered rows
  `{cName, nSize, nMethod, nCompSize, nRatio, dDate, cTime, cCRC,
  nInternalAttr, lCrypted, cComment}`.
- New `AdsZipListFiles` ACE export + `ZipList`/`ZipListAck`
  (`0x1B`/`0x1C`) wire ops, local and remote; bare archive names
  resolve under `backup/` like `UnZip`; needs no password.
- MinGW import libs refreshed with the new export (same
  dlltool-delta recipe as v1.09.60: cdecl + stdcall `@20` forms,
  stub-link verified). MSVC users generate from the shipped
  `src/openads_ace.def` as usual.

### Test Results

- New suites: count/names, verbose fields, missing/escape
  rejection, remote wire round-trip; wrapper verbose mapping proven
  field-for-field against a Python-built fixture over a static link
  (CRC, DOS-date granularity, comments, zero-size guards).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.67-windows-x64.zip`
- `openads-1.09.67-windows-x86.zip`
- `openads-1.09.67-linux-x64.tar.gz`
- `openads-1.09.67-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
