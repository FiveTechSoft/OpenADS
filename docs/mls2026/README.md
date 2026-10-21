# OpenADS for MLS 2026

This branch is the OpenADS build that MLS 2026 (MasterLink Software) runs in
production testing. It is fivetechsoft/openads `main` at **f41738d3**
(1.09.74) plus the fixes below. Everything here was tested against a real
MLS 2026 installation:

- DBF/CDX tables and ADT tables
- OpenADS REMOTE (`openads_serverd`) and OpenADS LOCAL
- a 64-bit Harbour 3.2 / FiveWin client built with MSVC

The binaries are attached to this fork's Release `v1.09.74-mls1`.

## Fixes on top of f41738d3

| # | Where | Problem | Fix |
|---|---|---|---|
| 1 | `src/network/session.cpp` | The server applied SetAOF only to the engine table. Ordered navigation runs on the ABI twin handle (`tbls_h_`), so filtered, indexed browses showed wrong or partial rows. | Mirror SetAOF/ClearAOF onto the twin, including a twin created after the AOF was set. |
| 2 | `src/network/session.cpp` | The server acknowledged an AOF it could not optimise (e.g. `substr(glscod,1,4)=...`). Stock rddads only filters on the client when AdsSetAOF fails, so SET FILTER did nothing under REMOTE. | Reply `AE_INVALID_EXPRESSION`, as local AdsSetAOF does. The reply frame is built by hand, so the expected refusal stays out of the error log. |
| 3 | `src/engine/table.cpp` | `goto_record` did not update `sequence_idx_`, so `DbGoto(n)` + `DbSkip(-1)` under an AOF gave BOF. This affects LOCAL too. | Re-anchor the sequence index on the row. |
| 4 | `src/abi/ace_exports.cpp` (`AdsSkip`) | A pair-certified GoTop/GoBottom moved only the client, so the next index Skip walked from the wrong server row. | Anchor the server cursor with `goto_record` when `pair_local_position` is set. |
| 5 | `src/abi/ace_exports.cpp` (`adt_spec_for`) | ADT numeric fields with decimals were created as DOUBLE (type 10), which stores no decimal count. DbStruct reported 0 decimals, so values displayed rounded (7.75 showed as "8"). | `N` with `dec > 0` is created as ADT type 2 (numeric as ASCII digits), keeping the declared width and scale. |

The full source change is the commit on this branch. It is also in
`mls-patches-on-f41738d3.diff` in the Release.

## Build (MSVC only)

Visual Studio 2022 with CMake. The tested builds' CMake cache has generator
"Visual Studio 17 2022" (x64 / Win32), tests off, HTTP and SQLite on, every
other optional backend off. An equivalent configure:

```
cmake -S . -B build\mls-x64 -G "Visual Studio 17 2022" -A x64 ^
  -DOPENADS_BUILD_TESTS=OFF -DOPENADS_WITH_HTTP=ON -DOPENADS_WITH_SQLITE=ON
cmake --build build\mls-x64 --config Release

rem 32-bit client DLL: same, with -A Win32 into build\mls-x86
```

Outputs:
- `src\Release\openace64.dll` / `openace32.dll` and the matching `.lib`
- `tools\serverd\Release\openads_serverd.exe`

The `.lib` files are name-based import libraries. An application linked
against them can swap in a newer OpenADS DLL without relinking.

## Known problems with GCC/MinGW builds (do not use with MSVC apps)

A Winlibs GCC 10.2 build of the same code failed in two ways with an
MSVC-built Harbour application:

1. **Error 7079 "Connect: path outside data directory".**
   - `path_has_prefix()` in `src/platform/path.cpp` compares case-sensitively.
   - MSVC's `std::filesystem::weakly_canonical()` returns the on-disk case of an existing folder. GCC 10's libstdc++ keeps the caller's case.
   - So a client path `C:\mls2026s\Data\` against `--data C:\MLS2026s` is accepted by the MSVC server and refused by the GCC one.
   - On Windows the jail check should compare case-insensitively.
2. **Every number formats as stars.** With the GCC `openace64.dll` loaded into the MSVC-built exe:
   - All numeric output in the host program became `****`, even the exe's own file size in its error log.
   - It happened with plain DBFCDX too, where the DLL is loaded but not called.
   - Saves then wrote `**********` into DBF numeric fields, which corrupted tables.
   - Swapping only that DLL back to the MSVC build fixed it.

## Open items (not fixed here)

- `IndexKey(n)` / `OrdName(n)` past the last tag return tag 1 instead of
  `""`. Code that walks tags with `while ! Empty( IndexKey( j ) )` (the
  standard FiveWin/Harbour error handler) loops until it runs out of memory.
  Bound such loops with `OrdCount()`.
- REMOTE writes to DBF `N` fields are left-aligned and keep float digits
  (`"500       "`, `"199.449999"` in an N(10,2)) instead of right-aligned
  with the declared decimals. Harbour reads them, but they are not standard
  DBF numerics.
- REMOTE `DbCreate()` without an extension creates a `.dbf` even in ADT mode.
- SQL `CREATE TABLE [x.adt]` writes a VFP DBF named `.adt`.
- A value read right after `DbCommit()` + `DbUnlock()` under REMOTE was stale
  once (client row cache suspected).

## License

OpenADS is Apache License 2.0 (see `LICENSE`). These changes are released
under the same license.
