# Server-side UDFs via `.hrb` — design (notes from LetoDB)

## Goal

Let app functions (index-key UDFs like `Reverse()`, plus server-side
routines such as backups) execute **inside `openads_serverd`**,
loaded from a developer-supplied Harbour `.hrb` placed next to the
server. Precedent: Kresin’s original LetoDB `letoudf.hrb`
(SourceForge `p/letodb`, `source/server/server.prg`) — verified in
source. (The elch LetoDBf fork carries the same loading shape, but
per project guidance it is NOT the reference: it forked to fill
the MT gap and never succeeded. Canonical upstream is Kresin’s
LetoDB; our threading design stands on its own.)

## How LetoDB does it (verified in Kresin’s source)

- **File convention**: `letoudf.hrb` in the server base dir
  (`cDirBase + "letoudf.hrb"`). The original additionally lets its
  Planner load further `.hrb` task modules
  (`pPlan["hrb"] := hb_hrbLoad(...)`); the fork also accepts an
  alternate name through the reload command.
- **Gate flag**: server option `UDFEnabled`; with it off, remote
  functions are refused (`"UDF Error: using remote functions is
  disabled."`). Missing file without debug is silent — functions
  simply unavailable.
- **Lifecycle**: loaded once at startup before serving
  (`leto_HrbLoad()`); `UDF_Init` called if exported; `UDF_Exit`
  at shutdown; **hot reload without restart** via management
  message (`LETOCMD_udf_rel`, CLI `reload` command) which unloads
  (`hb_hrbUnload`) and re-loads, with load errors caught and
  logged (`BEGIN SEQUENCE … RECOVER`).
- **Dispatch**: resolve dynamic symbol, `hb_vmPushDynSym` + push
  args + `hb_vmDo(n)` inside `hb_vmRequestReenter()`, errors
  captured (`hb_xvmSeqBegin/End`), per-thread VM state set before
  the call (`letofunc.c`, task dispatch).
- **Key architectural insight**: LetoDB's server **is** a Harbour
  program, so expressions compiled server-side resolve UDF names
  against the loaded HRB for free. OpenADS's server is C++ with a
  hand-rolled evaluator — we must bridge explicitly (below).

## OpenADS mapping

- **Embed `hbvm`/`hbrtl` behind a build flag**
  (`OPENADS_WITH_HARBOUR_UDF`, default off) so stock builds stay
  dependency-free. Harbour's license exception permits linking
  into the server binary — confirm with project licensing before
  enabling by default anywhere.
- **Module location**: `--udf-module <path>` flag + ini key,
  default `<serverdir>/openads_udf.hrb`. (Accepting LetoDB's
  `letoudf.hrb` name as an alias costs nothing and eases
  migration — decide at implementation.)
- **Lifecycle**: load at startup, call `UDF_Init` if exported,
  `UDF_Exit` at shutdown, log module hash + exported function
  list. Reload channel TBD (SIGHUP vs. admin wire op — LetoDB
  uses a management message; mirror that).
- **Call shape A — scalar expression functions** (index keys,
  `FOR` conditions): in `apply_scalar_fn`, unknown builtin →
  registry lookup → bridge call (engine `Value` ↔ `HB_ITEM`,
  **byte-exact, no codepage translation**; stable return width
  per tag). Name missing everywhere → `AE_INVALID_EXPRESSION`
  at create time (fail fast, never a degenerate tag).
- **Call shape B — server-side procedures** (backups, maintenance
  jobs): invoked by admin/client command with an argument array,
  mirroring LetoDB's task dispatch (dynsym + `hb_vmDo` +
  error capture). Define one wire op, not one per routine.

## Hard constraints

1. **Purity for shape A**: deterministic, side-effect-free, no DB
   I/O inside. Maintenance calls it on every write; side effects
   corrupt or deadlock. Document; consider a debug-mode
   re-entrancy tripwire later.
2. **Threading**: builds/maintenance run on worker threads —
   per-thread VM attach / `hb_vmRequestReenter`, exactly as
   LetoDB does. Prototype this first; it is the main risk.
3. **Performance**: one VM call per record per tag on build and
   per write on maintenance. Correctness first; bulk-build cost
   is acceptable, note it in docs.
4. **Version coupling**: the `.hrb` must match the app version —
   a stale module builds wrong keys silently. Log names + hash
   at startup; docs state the coupling.
5. **Trust**: arbitrary pcode in the server process (an infinite
   loop hangs a worker). Vendor-trusted file + supervised
   process, same boundary as stored procedures. `UDFEnabled`-style
   kill switch ships with it (default: off unless module loads).

## Rollout

1. Fail-fast validation on unknown index functions (no silent
   empty keys).
2. Loader + bridge behind the flag; builtins (REVERSE, …) continue
   in parallel — most tag UDFs may never need the HRB path.
3. Pilot: Vouch tag expressions, then one shape-B routine
   (backup).
4. Reload channel + operator docs.

## Open questions

- Default module name/location (`openads_udf.hrb` vs
  `letoudf.hrb` alias).
- Reload via signal vs. admin wire op.
- Allowed value types across the bridge (memo? datetime?).
- Whether shape-A functions may open other tables (LetoDB UDF
  areas allow it; index purity says no — decide per shape).
