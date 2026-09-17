# OpenADS v1.09.52

## Server-side Harbour UDFs via loaded `.hrb` module

Tags whose key expression calls app functions no longer depend on
hardcoded builtins. `openads_serverd` embeds the Harbour VM and
loads a developer-supplied `.hrb` (LetoDB `letoudf.hrb` pattern —
canonical upstream is Kresin's LetoDB); its functions become
available to index expressions and `FOR` conditions evaluated on
the server. v1.09.51's native `REVERSE()` keeps working with no
module needed.

### Usage

- `--udf_module PATH` (ini: `udf_module`), default
  `openads_udf.hrb` next to the server binary, loaded when present.
- Server logs the loaded function list at startup; a
  missing/unloadable module fails startup loudly instead of
  building wrong indexes.
- Build flag `OPENADS_WITH_HARBOUR_UDF` (default OFF). The Linux
  release leg builds it (Harbour SDK compiled from source in CI,
  cached); Windows/macOS legs stay OFF for now. Core and the ACE
  DLL never embed the VM (a Harbour client process has its own).

### Constraints (enforced by design, see docs/server-udf-hrb-design.md)

- Index UDFs must be deterministic, side-effect-free scalars over
  the passed args (same contract as the native builtins).
- Only RTL builtins resolvable in a bare host link at load; anything
  else fails the load with the missing symbol named. Module
  `STATICs` do not resolve across calls — keep functions stateless.
- Calls are serialised process-wide; a UDF runtime error unwinds
  to the bridge (call fails cleanly, VM stays usable).

### Changes

- New: `src/engine/hrb_udf.*` (provider interface, core stays
  Harbour-free), `src/engine/hrb_harbour.*` (VM backend: load with
  `UDF_Init`, `UDF_Exit` on unload, protected calls with real
  Harbour error text), evaluator fallback in `index_expr.cpp`,
  `--udf_module`/ini/plumbing in serverd.
- Tests: `tests/hrb/udf_test.prg` compiled to `.hrb` at build time;
  load/list/init, scalar round-trips, blanks preserved,
  date→YYYYMMDD, error containment with post-error usability, and
  end-to-end `TESTREV(NAME)` tag build + seek + maintenance.
- CI: Linux release leg provisions a pinned Harbour SDK
  (harbour/core @ 4ec2e15, cached); publish stays all-or-nothing.

### Test Results

- Local HRB proof build (MinGW x86 + Harbour 3.2 SDK): 4/4 HRB
  cases green; full unit suite 1578/1579 (sole failure
  pre-existing, MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.52-windows-x64.zip`
- `openads-1.09.52-windows-x86.zip`
- `openads-1.09.52-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.52-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
