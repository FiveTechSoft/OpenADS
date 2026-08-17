## v1.8.88

### Bug Fixes

- **`oads_hb.c` includes `"ace.h"`** (Pritpal Bedi).

  `contrib/oads_hb/oads_hb.c` is compiled into your Harbour project,
  not into the OpenADS DLL. It now includes `"ace.h"` the same way
  `contrib/rddads` does (`HB_WITH_ADS` / ACE SDK). Drop the file in
  and build — no more editing `#include "openads/ace.h"` on every
  compile.

### Packaging

- **One Windows ZIP name.** Canonical assets:

  - `openads-1.8.88-windows-x64.zip`
  - `openads-1.8.88-windows-x86.zip`

  Do not use `*-win-x64.zip` / `*-win-x86.zip`. That short name was a
  hand-rolled slim kit (different `ace64.dll`, no `openace64.dll` /
  bench / docs) that sat next to the CI zip on v1.8.84–v1.8.86.

### Files

**x64 (64-bit):**

- `openads_serverd.exe` — server daemon
- `ace64.dll` / `openace64.dll` — ACE shared library (identical copies)
- `ace64.lib` — MSVC import library
- `libace64.a` — MinGW import library
- `ace64_borland.lib` — Borland import library
- `openads_bench.exe` — bench CLI

**x86 (32-bit):**

- `openads_serverd.exe` — server daemon
- `ace32.dll` / `openace32.dll` — ACE shared library
- `ace32.lib` — MSVC import library
- `libace32.a` — MinGW import library
- `ace32_borland.lib` — Borland import library
- `contrib/oads_hb/oads_hb.c` — Harbour HB_FUNC wrappers

**Full Changelog**: https://github.com/FiveTechSoft/OpenADS/compare/v1.8.87...v1.8.88
