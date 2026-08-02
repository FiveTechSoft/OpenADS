# Import libraries

Link-time import libraries for the OpenADS engine DLL, one set per compiler
toolchain. They resolve the `Ads*` entry points to `ace64.dll` (x64) /
`ace32.dll` (x86) so an application links against OpenADS by name and loads
the engine at run time.

```
x64/                       x86/
  msvc/    ace64.lib          msvc/    ace32.lib     (COFF — MSVC / cl.exe)
  mingw/   libace64.a         mingw/   libace32.a    (GNU  — MinGW-w64 / gcc)
  borland/ ace64.lib          borland/ ace32.lib     (Borland / C++Builder)
```

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

## Regenerating

These are committed because the release CI runners have no Borland toolchain.
Regenerate after any change to the export surface (`src/openads_ace.def`,
`src/abi/ace_stdcall_x86.c`, `src/openads_ace_x86_stdcall.def`):

```powershell
cmake --build build/msvc-x64    --config Release --target openads_ace
cmake --build build-x86         --config Release --target openads_ace
pwsh dist/import-libs/gen.ps1
```

See `gen.ps1` for the exact tool invocations and expected install paths.
