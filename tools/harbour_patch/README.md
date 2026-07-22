# Harbour rddads compatibility patches

Two small patches against `harbour/contrib/rddads/` are required so the
standard rddads contrib links cleanly against `ace64.dll`/`ace.h` from
this repo and exposes the same record-state semantics as Harbour's
clean-room `dbfcdx` (which the `tests/rddtest/rddtst.prg` baseline was
recorded against).

Apply by hand or via `git apply` from a Harbour source tree:

```sh
cd /path/to/harbour
git apply --check /path/to/openads/tools/harbour_patch/rddads-compat.patch
git apply --3way /path/to/openads/tools/harbour_patch/rddads-compat.patch
```

Then rebuild rddads:

```sh
HB_WITH_ADS=/path/to/openads-sdk \
    bin/win/msvc64/hbmk2 contrib/rddads/rddads.hbp -comp=msvc64
```

The patch is checked with `git apply --check` against a clean Harbour
checkout. If nearby Harbour code has moved, use `git apply --3way` as shown
above so Git matches the surrounding function context instead of depending
only on recorded line numbers.

## Patch contents

### `contrib/rddads/adsfunc.c`

Adds `HB_FUNC( ADSCREATEINDEX )` — Harbour-level wrapper around
`AdsCreateIndex61` for the current ADSCDX work area:

```xbase
AdsCreateIndex( cBagFile, cTag, cExpr, [cFor], [nOptions] ) -> lOk
```

OpenADS' `ace64.dll` implements remote `AdsCreateIndex61` and accepts
`hOrdCurrent` (a remote index handle) in `AdsGotoTop` / `AdsSkip`.
Until this patch is applied and `rddads` rebuilt, link the standalone
`tools/harbour_patch/ads_create_index.c` into your `hbmk2` project
(do not link both — duplicate symbol).

### `contrib/rddads/rddads.h`

Adds an inline `ADSFIELD( UNSIGNED16 n )` helper, guarded by
`#ifndef ADSFIELD`. Harbour's rddads sources (`ads1.c`, `adsfunc.c`)
reference `ADSFIELD( n )` to fetch the n-th field's name as
`UNSIGNED8*` from the current workarea, but no upstream header in the
Harbour build defines it. The shim looks up the workarea via
`hb_rddGetCurrentWorkAreaPointer()` and reads the field's symbol name
through `hb_dynsymName`. (Without this patch `AdsGetMemoDataType()`
and ~30 other `HB_FUNC` wrappers fail to link.) The `#ifndef` guard
lets a newer `ace.h` that already provides an `ADSFIELD` macro (field
index encoded as a pointer, decoded server-side) take precedence.

Also adds the `ADSAREA` work-area fields backing the FetchWhere
fast-path below (`fFwEligible`, `fFwActive`, `szFwExpr`, `pFwRecNos`,
`ulFwCount`, `ulFwPos`).

Also pre-defines `ADS_MAX_PARAMDEF_LEN` to `2048`. Harbour's
`adsfunc.c` re-`#define`s this constant to `2048` *after* it includes
`rddads.h`, which collides with `ace.h`'s `#ifndef`-guarded `256`
default and trips `-Wmacro-redefined`. Pre-defining the `2048` ceiling
here satisfies `ace.h`'s guard, so `adsfunc.c`'s later `#define`
becomes an identical, warning-free redefine.

### `contrib/rddads/ads1.c`

Aligns the not-positioned `hb_adsSkip()` branch with `dbf1.c`'s
`hb_dbfSkip()`: setting one direction flag clears the opposite,
distinguishing real Bof / Eof from the Limbo state of a
freshly-opened empty table. Without this patch the rddtst sequence
`USE empty → DBSKIP(±1) → DBSKIP(0)` keeps both flags set instead of
cleanly transitioning to a single-flag state.

#### Tier-2 FetchWhere fast-path (non-AOF `SET FILTER` forward scans)

A `SET FILTER` whose predicate the server cannot optimise with an
index (`AdsGetAOFOptLevel == NONE`) makes the classic rddads scan test
the filter one *raw* record at a time — one network round-trip per
record (the remote bulk-scan bottleneck, ~135 s / 1M rows).

When the table is remote and no index is active, `adsSetFilter` marks
the work area eligible and `adsGoTop` primes it: the server evaluates
the predicate and returns matching rows — record number **and every
field value** — in batches via `AdsFetchWhere` (`WANT_RECNO`). A
forward scan walks those rows straight from the client row cache via
`AdsFetchWhereApplyRow`, so `RecNo()` / field reads serve from cache
with **no `AdsGotoRecord` per match** — the whole scan costs
`ceil(matches / batch)` round-trips. On the first prime the server
AOF/filter is cleared so any non-forward move (GOTO, GO BOTTOM,
backward SKIP, write, index/filter change) resyncs the real server
cursor onto the last visited record and resumes the classic
client-side walk via SUPER's local filter block — correct Clipper
semantics, and it also sidesteps the AOF bitmap-navigation edge cases.
The `AdsFetchWhere*` result-set API (including `AdsFetchWhereApplyRow`)
is an additive OpenADS engine export; no existing ACE function is
touched.

Validated end-to-end against a live `openads_serverd` over `tcp://`
with the `fwscan.prg` golden harness (COUNT / forward scan / LOCATE /
GOTO+SKIP / backward SKIP / field reads, all matching the DBFCDX
baseline).
