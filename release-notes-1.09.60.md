# OpenADS v1.09.60

## Fix: MinGW import libs were missing the new exports (v1.09.59 postmortem)

v1.09.59 shipped with `AdsZipFiles`/`AdsUnzipFiles` in the DLLs,
but Harbour/MinGW apps (Vouch) link the **prebuilt import libs**
under `dist/import-libs/`, which still ended at the v1.09.28
exports — so `OAds_Zip` compiled (fresh `oads_hb.c`) yet failed to
link. The `ace64.dll`/`ace32.dll` themselves were always fine.

### Changes

- Regenerated `dist/import-libs/x86/mingw/libace32.a` (cdecl +
  stdcall `@56`/`@36` forms, decorations verified against
  `AdsFOpen@16`-era ground truth) and
  `x64/mingw/libace64.a` by merging dlltool-built delta archives
  (same recipe as `dist/import-libs/gen.ps1`, whose MSVC/Borland
  toolchains are absent here). Link-verified with a stub consumer.
- MSVC/Borland import libs are unchanged: MSVC users generate them
  from the shipped `src/openads_ace.def`, which already lists the
  new exports (`lib /def:openads_ace.def /machine:X64`).
- No product-code changes (server-side HRB UDF loading + ZIP
  archiving ship here unchanged).

### Vouch integration (required on the app side)

- Sync `contrib/oads_hb/oads_hb.c` from this release into the Vouch
  project (it carries `HB_FUNC( OADS_ZIP )` / `HB_FUNC( OADS_UNZIP )`)
  and recompile Vouch.
- Relink against the refreshed `libace32.a` (MinGW) from this
  release's `dist/import-libs` (also inside the zips under `lib/`).
- No `rddads` rebuild involved: `OAds_*` calls go straight to ACE,
  not through the RDD.

### Test Results

- New unit suites as in v1.09.59 notes; import-lib merge verified
  by symbol inspection + stub link.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.60-windows-x64.zip`
- `openads-1.09.60-windows-x86.zip`
- `openads-1.09.60-linux-x64.tar.gz`
- `openads-1.09.60-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
