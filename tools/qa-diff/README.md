# qa-diff — differential xBase QA for OpenADS

Runs the **same** classic xBase operations through pure Harbour (rddads, no
ORM) against two RDDs and diffs the output. The native RDD is the **oracle**:
where native gives the textbook result and OpenADS differs, you have a
candidate bug. This catches "boring" correctness bugs that unit suites miss —
`INDEX ON … FOR …`, `REINDEX`, `SEEK`, ascending/descending walks, `SET FILTER`,
`ordScope`, `LOCATE`, `PACK/ZAP`, conditional indexes, memo round-trips.

Compared same-family (apples to apples):

| Oracle (native) | System under test (OpenADS) |
|-----------------|-----------------------------|
| `DBFCDX`        | `ADSCDX`                    |
| `DBFNTX`        | `ADSNTX`                    |

## Files
- `qamatrix.prg` — the ~17-step operation matrix; writes a normalized,
  diffable log (one labelled line per checkpoint). Errors are trapped and
  logged (no blocking GUI alert) so a runtime failure is recorded, not hung.
- `repro.prg` — minimal **isolated** reproducers (each test = fresh table, no
  state cascade) for the divergences worth filing as bugs.
- `qamatrix.hbp` / `repro.hbp` — link lines (`-lrddads -L${OPENADS_LIB}
  -l${OPENADS_ACELIB} -lrddcdx -lrddntx -lrddfpt`).
- `run.cmd` — portable build+run+diff driver. No baked-in paths.

## Usage
```cmd
:: from an MSVC x64 dev prompt, with hbmk2 + rddads available
run.cmd <folder-with-openace64.dll-and-.lib>
```
It builds `qamatrix`, runs it for DBFCDX/DBFNTX/ADSCDX/ADSNTX, then `fc`-diffs
native vs ADS. Differing lines = candidate bugs.

> Toolchain note: a headless build works with Harbour (MSVC64) + a portable
> MSVC whose `setup_x64.bat` provides the Windows SDK, plus the CRT-compat link
> flags carried in `run.cmd`. See the cookbook `console/build.cmd` for the same
> recipe.

## Methodology caveat
Native uses `rddcdx`/`rddntx`; ADS uses `rddads → openace64`. A divergence is
not *necessarily* an engine bug — it can live in the rddads→ABI mapping.
**Confirm engine-level findings with an ABI doctest** (no rddads) before filing
them as engine bugs. Example: `tests/unit/abi_qa_repro_test.cpp` confirms the
conditional-`FOR` logical-field bug at the ABI; a numeric `ordScope` divergence
seen here, by contrast, passes at the ABI (so it is a mapping issue, not engine).
