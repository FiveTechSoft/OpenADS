# Windows installer (Inno Setup)

`openads-setup.iss` builds a classic Windows `setup.exe` for OpenADS — the
clicky front door ex-Advantage users expect. It is a thin GUI shell over the
**same** machinery the cross-platform console wizard (`openads_serverd
--setup`) uses: it asks for the data directory, wire port, Studio console and
auto-start, writes an `openads.ini`, and (optionally) registers the Windows
service with `openads_serverd --install-service --config <ini>`. No install
logic is duplicated — Linux/macOS use the console wizard directly.

## Building it

You need [Inno Setup](https://jrsoftware.org/isinfo.php) (`ISCC.exe`) on a
Windows box, plus a **staging folder** containing the built binaries — for
example an extracted release zip or the output of `cmake --install` / `cpack`:

```
ace64.dll  openace64.dll  openads_serverd.exe  openads_bench.exe
openads-studio.bat  openads.ini.sample  QUICKSTART.md
LICENSE  NOTICE  README.md  lib\...
```

Then:

```bat
iscc /DSrcDir=..\..\dist\openads-1.4.0-windows-x64 /DAppVer=1.4.0 openads-setup.iss
```

- `SrcDir` — path to the staging folder (required in practice; defaults to `staging`).
- `AppVer` — version string for the installer; pass the release version so the
  output is `openads-<ver>-setup.exe`. CI should pass the git tag, so nothing
  is hard-coded (mirrors how the CMake/CPack version is derived).

The `.exe` lands in `output\`.

## What the user sees

1. License + install folder (standard Inno pages).
2. **Server settings** — data directory, wire port (6262 default), Studio port,
   optional Studio admin user/password.
3. **Options** — enable the Studio web console; start automatically as a service.
4. Install → writes `openads.ini`, creates the data folder, registers the
   service if asked. A Start-menu shortcut launches **OpenADS Studio** (the
   browser admin console).

Uninstall removes the service first, then the files.

## Release ZIP names

Canonical GitHub Release assets (produced by `.github/workflows/release.yml`
and by CPack) are:

- `openads-<ver>-windows-x64.zip`
- `openads-<ver>-windows-x86.zip`

**Do not** create or upload a parallel `openads-<ver>-win-x64.zip` /
`openads-<ver>-win-x86.zip`. That short name was a hand-rolled slim kit
(FiveTechSoft account, 5 files, a different `ace64.dll`) that sat next
to the CI zip on v1.8.84–v1.8.86 and looked like a duplicate.

To publish: push the `v*` tag and let CI attach the `windows-*` zips.
Do not `gh release create` / `gh release upload` a local `*-win-*.zip`.

## Notes

- The installer requires elevation (`PrivilegesRequired=admin`) because the
  service registration and a Program Files install both need it.
- This script is **Windows-only** by nature. It is not built by the default
  CMake/CPack flow; wire an `iscc` step into the Windows release leg of CI when
  you want a signed `setup.exe` alongside the zip.
- The staging files marked `skipifsourcedoesntexist` (studio launcher, ini
  sample, QUICKSTART) come from the onboarding/config PRs; the installer still
  builds without them, just with fewer extras.
