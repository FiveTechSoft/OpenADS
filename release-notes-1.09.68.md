# OpenADS v1.09.68

## `OAds_UnZip` destination defaults to the archive's dir

`OAds_UnZip` had no answer for "where" when the caller holds only
the archive: the destination is now optional, mirroring Harbour's
`hb_UnzipFile` `cPath` rule:

```
OAds_UnZip( [hConn,] cDir, cZip [, cPassword [, lOverwrite [, lWithPath ]]] )
OAds_UnZip( [hConn,] cZip )   -- extract next to the archive
```

An empty or omitted `cDir` extracts next to the archive (bare
names land in `backup/` beside it). Explicit destinations behave
exactly as before, under the same jail.

### Test Results

- New suites: empty-dir extraction next to explicit-subdir and
  dated archives, plus a remote wire case; full zip file 17/17
  green on MinGW x86/x64.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.68-windows-x64.zip`
- `openads-1.09.68-windows-x86.zip`
- `openads-1.09.68-linux-x64.tar.gz`
- `openads-1.09.68-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
