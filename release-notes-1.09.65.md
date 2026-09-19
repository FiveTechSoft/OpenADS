# OpenADS v1.09.65

## Zip destination accepts fully-qualified paths (field fix)

`OAds_Zip`'s `cZipDir` rejected anything with a drive letter, so
field calls shaped `OAds_Zip( "c:\creative.tra\cac00001\", aFiles,
"c:\creative.bkp\", "tra0100.fsi", ... )` died with `bad archive
name`. The destination now folds exactly like source dirs do: an
absolute/drive spelling (`c:\creative.bkp\`) becomes the
root-relative remainder (`creative.bkp`) under the owning data
root — same jail, same verbatim filename, no date or extension
added. A bare `creative.bkp` behaves as before. `..` above the
root still fails loud.

## MinGW shared DLL links again

`ace32.dll` would not link under GNU ld: the x86 stdcall wrappers
ride on an MSVC-only `#pragma comment(linker, "/alternatename")`.
Per the documented intent (wrappers exist for MSVC callers, not
GNU ld), MinGW builds now skip that TU and export the plain cdecl
def directly — same undecorated names cdecl callers already used.
MSVC builds are byte-identical to before.

### Test Results

- Verbatim-naming suite extended with a drive-spelling case
  (`C:/absbkp/nightly2`, host-independent folding, green on MinGW
  x86/x64); full zip file 11/11 green locally.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.65-windows-x64.zip`
- `openads-1.09.65-windows-x86.zip`
- `openads-1.09.65-linux-x64.tar.gz`
- `openads-1.09.65-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
