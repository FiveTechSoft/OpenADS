# OpenADS v1.09.63

## OAds_Zip: explicit source/destination args + verbatim archive names

`OAds_Zip` takes a new argument list with the archive destination
split in two (field request, Vouch backup flow):

```
OAds_Zip( [hConn,] cSrcDir, aSrcFiles, cZipDir, cZipName
          [, nLevel [, lOverwrite [, cPassword [, aExclude
          [, lWithPath ]]]]] )
   -> { nFiles, nBytes, nArchiveBytes, cArchive }, NIL on failure
```

- `cSrcDir`, `aSrcFiles`, `cZipDir`, `cZipName` are mandatory; the
  rest default to `6, .F., "", {}, .F.`. `hConn` stays optional
  (default-connection overload kept).
- Empty `cZipDir` keeps the legacy dated repository
  (`<root>/backup/<name>_YYYYMMDD.zip`); any non-empty dir writes
  verbatim to `<root>/<cZipDir>/<cZipName>` — the filename is
  application-dependent, **no date or extension is added**.
- The subdir is jailed under the data root (created on demand,
  `..`/drive escapes fail loud) and works local and over the wire
  with no wire-protocol or `AdsZipFiles` ABI change, so the v1.09.60
  import libs stay valid. The reported `cArchive` spelling feeds
  straight back into `OAds_UnZip`.
- **Breaking:** the v1.09.59 3-arg form is not accepted — old calls
  return `NIL` loudly instead of misfiling archives.

## OADS_DIRECTORY() returns a Harbour array

`OADS_DIRECTORY()` previously returned the engine's packed binary
buffer as a string. It now returns `Directory()`-shaped
`{ {cName, nSize, dDate, cTime, cAttr}, ... }` (`""` attr for plain
files; only `D`/`R` bits exist server-side). Empty array when
nothing matches or on error.

## Release engineering: MinGW provisioning without -Syuu

The v1.09.61/v1.09.62 tags never shipped: both Windows legs died
deterministically in *Set up MinGW for the static ACE library*
(`.61` was a real compile break, fixed; `.62` fails in the MSYS2
setup step itself, which no repo diff can influence — upstream
`setup-msys2` code unchanged since June, pointing at MSYS2
mirror/db-sync rot against the full `pacman -Syuu` upgrade path).
The release now uses the runner image's preinstalled MSYS2
(`release: false`) and installs only the toolchain
(`update: false`) instead of downloading a months-old base plus a
full system upgrade.

### Test Results

- New `zip: explicit subdir takes the filename verbatim` suite
  (verbatim name, nested auto-create, overwrite rules, unzip
  round-trip, traversal rejection); touched C++ TUs plus the
  wrapper file compile-verified (`g++ -fsyntax-only`, `hbmk2`).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.63-windows-x64.zip`
- `openads-1.09.63-windows-x86.zip`
- `openads-1.09.63-linux-x64.tar.gz`
- `openads-1.09.63-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
