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

## S4 parity gates (SAP ace64.dll vs OpenADS openace64.dll)

Separate from the Harbour matrix above: `s4_parity*.ps1` run identical SQL
through both engines against per-engine sandbox copies and diff the JSON.
SAP is the oracle. Two corpora:

| Gate | Corpus | Sandboxes |
|---|---|---|
| `s4_parity.ps1` | pmsys — property mgmt, ADT-only, 7912 perm rows | `F:\tmp\parity\{sap_data,oa_data}` |
| `s4_parity_mp.ps1` | mp10 — medical billing, 9.65M rows, mixed ADT/DBF, 63801 perm rows | `F:\tmp\parity\{sap_data_mp,oa_data_mp}` |

Two corpora exist because a single one cannot prove 1:1 — mp10 immediately
surfaced five gap classes pmsys structurally cannot reach (deleted-record
visibility on DBF, `COUNT(DISTINCT)`, aggregates over expressions, `Length()`,
view resolution). See `TODO.parity.md`.

### Rebuilding the mp sandboxes

mp10's tables were SAP-encrypted; `tools/decrypt_dd` decrypted them in place,
which is what made this corpus possible at all.

The dictionary password is never stored in this repo — it is public. Supply it
per run; both gates read `OPENADS_PARITY_PW` (or take `-Password`).

```powershell
$env:OPENADS_PARITY_PW = Read-Host 'DD password' -AsSecureString `
    | ForEach-Object { [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($_)) }

# 1. SAP side - copy the data dir, skipping backups and the transaction log.
#    ~67 GB / 300 files. Most of it is .adm memo blobs; copy them anyway so a
#    missing memo never gets mistaken for an engine bug.
robocopy "\\172.16.0.138\e$\adsdata\sfi" "F:\tmp\parity\sap_data_mp" `
         /E /MT:16 /R:1 /W:1 /XF *.BAK *.bak *.txlog

# 2. OpenADS side - byte-identical duplicate, so any diff is the engine.
robocopy "F:\tmp\parity\sap_data_mp" "F:\tmp\parity\oa_data_mp" /E /MT:16 /R:1 /W:1

# 3. Convert the dictionary to OpenADS native format.
F:\OpenADS\build\ninja-clang-local\tools\import_dd\openads_import_dd.exe `
    --source F:\tmp\parity\sap_data_mp\mp.add `
    --dest   F:\tmp\parity\oa_data_mp\mp_OpenADS.add `
    --user adssys --password $env:OPENADS_PARITY_PW --sap-lib F:\ads11\ace64.dll

# 4. Run the gate.
cd F:\OpenADS\tools\qa-diff; .\s4_parity_mp.ps1
```

Note `robocopy` may keep running after the byte count stops advancing; compare
file count + total size against the source rather than waiting on it to exit.

### Adding cases

Don't copy cases between corpora blind — the same-named object can differ.
mp's `sp_GetPhysicalPath` takes **no** arguments while pmsys's takes a table
name; check `system.storedprocedures.Proc_Input` first. Likewise verify column
names against `system.columns` rather than assuming (`payfile` has `CHARGES`,
not `amount`).

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
