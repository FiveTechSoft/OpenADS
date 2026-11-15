# Import libraries

Link-time import libraries for the OpenADS engine DLL, one set per compiler
toolchain. They resolve the `Ads*` entry points to `ace64.dll` (x64) /
`ace32.dll` (x86) so an application links against OpenADS by name and loads
the engine at run time.

```
x64/                          x86/
  msvc/    ace64.lib             msvc/    ace32.lib           (COFF — MSVC / cl.exe)
  mingw/   libace64.a            mingw/   libace32.a          (GNU  — MinGW-w64 / gcc)
           libopenace64.dll.a             libopenace32.dll.a  (GNU, for openace<bits>.dll)
  borland/ ace64.lib             borland/ ace32.lib           (Borland / C++Builder)
```

The MinGW `libopenace<bits>.dll.a` libs are the same as `libace<bits>.a` but
bound to the default `openace<bits>.dll` name; link them with
`-lopenace32` / `-lopenace64`. GNU ld picks `libX.dll.a` ahead of `libX.a`
in the same directory, so they never clash with the static
`libopenace<bits>.a` archive. (In the release zip the static archive sits at
the root and these under `lib/`, so `-L` order decides which one a
`-lopenace32` resolves to.)

- **MSVC** — `cl app.c ace64.lib`
- **MinGW / GCC (Windows)** — `gcc app.c -lace64` (with `-L` pointing here)
- **Borland / C++Builder** — link `ace64.lib` (bcc64, COFF) / `ace32.lib`
  (bcc32, OMF)

On **Linux / macOS** there is no import library: link the shipped
`libopenace64.so` / `.dylib` (or its `libace64.*` drop-in copy) directly.

All six libraries expose the same entry points and reference the
`ace64.dll` / `ace32.dll` drop-in name. Both `ace64.dll` and the default
`openace64.dll` are byte-identical, so an import lib built against either
name loads the same engine.

## x86 `__stdcall` decoration

The x86 DLL exports `__stdcall`-decorated (`_AdsXxx@N`) names matching what
MSVC-built Harbour `contrib/rddads` imports on 32-bit Windows, plus a plain
undecorated alias (`AdsXxx`, bound to the same `__cdecl` implementation) of
every entry point for MinGW-built rddads (cdecl `_AdsXxx` references) and
`GetProcAddress` probes. The MSVC `ace32.lib` merges the DLL's own link
byproduct with a `lib.exe`-synthesized cdecl supplement; the MinGW
`libace32.a` merges two `dlltool` passes over the DLL's export table
(`--no-leading-underscore` for the `@N` names, default mode for the plain
ones). Both libs therefore serve stdcall and cdecl callers. The x64 import
libs use the undecorated `src/openads_ace.def` (x64 has no `@N` decoration).

(Reported by JONSSON RUSSI, RusSoft Ltda.)

### Do not link `openace32.dll` itself with MinGW

GNU ld can link a DLL file directly (`gcc app.o openace32.dll`, or the DLL
path listed in an `.hbp`). **Do not do this on x86.** ld imports each DLL
export `X` as the symbol `_X`, so the stdcall exports `_AdsXxx@N` become
`__AdsXxx@N` and never match. ld's stdcall fixup then silently binds a
stdcall caller's `_AdsXxx@N` reference to the plain **cdecl** `AdsXxx`
alias (`Warning: resolving _AdsXxx@N by linking to _AdsXxx`). Every call
then leaves its arguments on the stack. Harbour rddads built against the
ACE SDK headers (stdcall) crashes at start-up in `ADS_GETFUNCTABLE` with
an access violation.

Link an import library instead: `-lopenace32` (`libopenace32.dll.a`) or
`-lace32` (`libace32.a`). Each maps `_AdsXxx@N` to the stdcall export and
`_AdsXxx` to the cdecl one, so both kinds of caller balance the stack. With
hbmk2, use `-L<dir> -lopenace32` rather than a bare `.a` path: a bare path
can land ahead of `-lrddads` on the link line, and ld only pulls archive
members for symbols already referenced.

## Regenerating

These are committed because the release CI runners have no Borland toolchain.
Regenerate after any change to the export surface (`src/openads_ace.def`,
`src/abi/ace_stdcall_x86.c`, `src/openads_ace_x86_stdcall.def`):

```powershell
cmake --build build/msvc-x64    --config Release --target openads_ace
cmake --build build/msvc-x86    --config Release --target openads_ace
pwsh dist/import-libs/gen.ps1
```

To regenerate just one toolchain, or with tools installed elsewhere:

```powershell
pwsh dist/import-libs/gen.ps1 -Toolchains mingw -MinGW32Bin S:\mingw492\bin -MinGW64Bin C:\mingw64\bin
```

See `gen.ps1` for the exact tool invocations and default install paths.
