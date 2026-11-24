---
title: rddads / X# RDD compat
layout: default
parent: Home (EN)
nav_order: 7
permalink: /en/rddads-compat/
---

# rddads / X# RDD compatibility

OpenADS' `ace64.dll` / `ace32.dll` is a **drop-in replacement** for
the Advantage Client Engine. Two third-party RDDs link against it
by name:

- **Harbour `contrib/rddads`** — covered end-to-end since
  v0.1.0-rc1 (the first Harbour smoke).
- **X#'s `AXDBFCDX` RDD** (`ADSRDD.prg`) — covered locally + over
  the wire since **v1.0.0-rc19** (M12.22 / M12.23).

This page documents the X# RDD work and is the canonical reference
for what each of the X#-specific exports actually does.

## How X# binds OpenADS

X# `ADSRDD.prg` performs `LoadLibrary("ace32.dll")` (or `ace64.dll`)
and resolves every entry point **by name** through
`GetProcAddress`. The X# Advantage RDD calls a much wider surface
than Harbour's `contrib/rddads`, including a set of versioned
overloads (`AdsCreateTable90`, `AdsCreateIndex90`, etc.) that exist
to absorb the charset / collation / page-size parameters newer ACE
builds added.

OpenADS therefore exports every `Ads*NN` name X# binds; most
forward to the base signature, a few are accept-and-ignore for
session / statement toggles that don't apply to a clean-room
implementation, and the genuinely-unimplemented return
`AE_FUNCTION_NOT_AVAILABLE` so the X# runtime falls back to its own
client path.

## M12.22 — versioned ACE overloads

| Export                          | Behaviour |
|---------------------------------|-----------|
| `AdsConnect26`                  | Forwards to `AdsConnect60`. |
| `AdsCreateTable71` / `AdsCreateTable90` | Forward to `AdsCreateTable` (drop charset / collation / page-size). |
| `AdsOpenTable90`                | Forwards to `AdsOpenTable80`. |
| `AdsCreateIndex90`              | Forwards to `AdsCreateIndex61` after re-mapping flags. |
| `AdsDDAddTable90`               | Forwards to `AdsDDAddTable`. |
| `AdsDDCreateRefIntegrity62`     | Forwards to `AdsDDCreateRefIntegrity`. |
| `AdsFindFirstTable62` / `AdsFindNextTable62` | Forward to base. |
| `AdsGetDateFormat60`            | Forwards to `AdsGetDateFormat`. |
| `AdsGetExact22`                 | Forwards to `AdsGetExact`. |
| `AdsReindex61`                  | Forwards to `AdsReindex`. |
| `AdsRestructureTable90`         | Forwards to `AdsRestructureTable`. |
| `AdsGetBookmark60` / `AdsGotoBookmark60` | Round-trip the recno as a 4-byte blob. |
| `AdsCancelUpdate90` / `AdsSetProperty90` | Accepted no-ops. |
| `AdsFindConnection25` / `AdsGetTableHandle25` | Report not-found — OpenADS keys by handle, not path / name. |

## M12.23 — the X# export gap

A live run of X#'s `AXDBFCDX` against OpenADS' DLL surfaced ~45
more entry points `ADSRDD.prg` binds by name. The behaviour:

- **Field setters** (`AdsSetField`, `AdsSetEmpty`, `AdsSetNull`,
  `AdsSetShort`, `AdsSetMoney`, `AdsSetTime`, `AdsSetTimeStamp`) —
  all handle the ACE "field name *or* 1-based ordinal cast to a
  pointer" idiom (X#'s `_FieldSub` calls `AdsGetFieldType` /
  `Length` / `Decimals` by ordinal, passing a tiny pointer value
  the old code dereferenced as a string).
- **Field readers** (`AdsGetDate`, `AdsGetMemoBlockSize`,
  `AdsGetTableOpenOptions`, `AdsGetBookmark`) — real
  implementations.
- **Cursor helpers** (`AdsCancelUpdate`, `AdsContinue`,
  `AdsEval*Expr`) — `AdsCancelUpdate` is an accept-and-ignore;
  the others return `AE_FUNCTION_NOT_AVAILABLE` so X# falls back.
- **RI / unique / autoinc enforcement toggles** —
  accept-and-ignore (the underlying enforcement still happens
  through `AdsCreateIndex` / DD).
- **`AdsStmt*` helpers** — return `AE_FUNCTION_NOT_AVAILABLE`;
  X#'s SQL surface routes around them.

## Semantics fixes that shipped with M12.23

These looked like "missing exports" from the X# RDD's point of
view, but were actually wrong behaviour in existing entries:

- **`AdsAppendRecord` auto-locks the new record.** ACE semantics
  for non-exclusive tables — X#'s `GoHot` refuses to write a
  record it sees as unlocked.
- **`AdsIsRecordLocked` / `AdsLockRecord` / `AdsUnlockRecord`
  honour `recno == 0` = current record** and report the real
  lock state instead of stubbing `0`.
- **`AdsCreateIndex61` / `AdsCreateIndex90` option-bit fix.** The
  "descending" flag is `ADS_DESCENDING` (`0x08`), not `0x02` —
  `0x02` is `ADS_COMPOUND`, which X#'s ADSRDD always sets for
  CDX orders, so the old mask built every X# order descending
  and `DbGoTop` landed on the last key.
- **`AdsCreateTable` / `AdsCreateTable90` stage an empty `.fpt`
  next to the `.dbf`** when the field list has an `M` field
  (using `usMemoBlockSize`, default 64). Without it
  `Connection::open_table` can't attach a memo store and any
  memo write fails "memo store not attached".

## Remote-server X# (M12.23, rc19)

Three more fixes so X#'s ADSRDD drives `openads_serverd` over the
wire (`AdsConnect60("tcp://host:port/<datadir>",
ADS_REMOTE_SERVER) → AX_SetConnectionHandle → DbUseArea`):

- `remote_field_index` honours the "field name OR 1-based ordinal
  cast to a pointer" idiom — same as the local side.
- The remote `AdsOpenTable` branch defaults a missing extension
  to `.dbf` (X# passes the bare table name for remote tables).
- `AdsGetTableFilename` gained a remote path (returning the
  opened name) instead of failing `AE_INTERNAL_ERROR` — X#'s
  `Open` calls it right after `_FieldSub`.

## Test harness

```
tests/smoke/xsharp/
├── AdsSmoke.prg          # local: ace64.dll directly
└── AdsSmoke_remote.prg   # remote: openads_serverd over tcp://
```

Both pass end-to-end against OpenADS:

- **Local smoke**: `DbCreate` (incl. an M field) → `DbUseArea` →
  `OrdCreate` ×2 → `DbAppend` + `FieldPut` ×4 → `DbCommit` →
  NAME-order `GoTop` / `Skip ±` / `GoBottom` / `Eof` →
  `DbSeek` hit + miss → memo round-trip → `DbDelete` /
  `DbRecall` → replace a key field + re-read through the CITY
  order → `DbCloseArea`.
- **Remote smoke**: opens `customer.dbf` on the server and does
  read / nav (`RecCount` / `GoTop` / `Skip ±` / `GoBottom` /
  `Eof` / `FieldGet` / `OrdKeyCount`).

Doctest coverage: `tests/abi_versioned_overloads_test.cpp` (local)
and `tests/abi_remote_overloads_test.cpp` (over the wire, gated
on `-DOPENADS_TEST_REMOTE=ON`).

## Follow-ups landed in rc20+ from X# feedback

- **rc21 / M12.24** — `AdsGetLastTableUpdate` real signature
  matches ACE (`UNSIGNED8* pucDate, UNSIGNED16* pusLen`);
  `AdsSetDateFormat` is no longer a no-op; `AdsSetAOF` returns
  success + `ADS_OPTIMIZED_NONE` on non-optimisable expressions
  instead of error 7200.
- **rc22 / M12.25** — `AdsCreateTable` stamps today's date into
  the DBF header up front (matches ACE), so a fresh
  create+open reports today instead of `1900-00-00` until the
  first `DbAppend`.

## Known divergence: Skip on an empty table (OADS-CR-001 E15)

SAP ADS 11.10 (ace32, local) reports **both** `Bof()` and `Eof()` after
`Skip(1)`, `Skip(-1)` and `Skip(0)` on an empty table. OpenADS reports
the single direction flag (`Skip(1)` → EOF only, `Skip(-1)`/`Skip(0)` →
BOF only), matching Harbour's native DBFCDX/DBFNTX.

This is a deliberate, documented divergence, kept for three reasons:

1. **Harbour rddtst conformance.** The repo's
   `tools/harbour_patch/rddads-compat.patch` aligns rddads' Skip
   semantics with Harbour `dbf1.c` so the Harbour RDD test suite
   passes. Adopting SAP's both-flags Skip at the engine layer would
   resurface through rddads and re-break that suite unless the shim
   masked it again — two divergent behaviours for one flag.
2. **Impact is LOW** (the report's own rating). The only affected
   pattern is `IF Bof() .AND. Eof()` evaluated *after an explicit
   Skip* on an empty table. The common empty-table probes — right
   after `USE` and after `GoTop` — already report both flags in all
   three columns of the report.
3. **DBFCDX agrees.** Applications ported from Harbour's native RDDs
   see no change; only an application ported *from* SAP ADS that
   probes the flags after a Skip on an empty table would notice.

ACE-compatible alternative, if a real application ever depends on the
SAP behaviour: change `Table::skip()`'s empty-table branch to preserve
the Limbo (BOF+EOF) state for every delta, and add one hunk to
`rddads-compat.patch` so `hb_adsSkip`'s not-positioned branch collapses
the both-flags phantom to the single direction flag when the table is
empty (dbf1.c semantics at the Harbour layer, SAP semantics at the raw
ACE API). Note this moves parity to the ACE API only — Harbour/rddads
clients would still see DBFCDX semantics — and it changes wire-visible
state, so it requires a remote regression check against the pre-change
build before publishing.
