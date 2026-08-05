# Column-level permission enforcement — current state & implementation plan

Status: **PLANNED, implementation deferred** (2026-08-05). Full SQL-engine
parity currently outranks this; when it is picked up, start at Phase 0.

This document replaces the stale summary in `TODO.parity.md` ("310 of 389
grants never imported / enforcement missing entirely") — both halves of that
sentence are out of date. The survey below is what is **actually in the tree
today**, verified against source and the running test suite.

---

## 1. What exists today (verified 2026-08-05)

### Table-level (file-level) enforcement — DONE and tested

Confirmed by ~20 passing cases in `tests/unit/abi_dd_perms_test.cpp` plus the
DataDict unit tests. The pieces:

| Piece | Where |
|---|---|
| Permission model: `PermissionEntry` (object, type, grantee, group flag, bitmask), `DD_PERM_SELECT/UPDATE/INSERT/DELETE/EXECUTE/REFERENCE/GRANT/FULL` | `src/engine/data_dict.h` (~line 338) |
| Per-user effective-ops cache (`build_perm_cache` at connect → O(1) `check_perm` / `get_effective_ops`), group-union semantics, cache invalidation on grant | `data_dict.cpp` |
| Navigational open gate: `AdsOpenTable` denies per open mode when `usCheckRights` ≠ 0 (readonly → needs `select_`, else `update_`/`insert_`) | `ace_exports.cpp` (search `Per-table ACL check`) |
| SQL statement gate: SELECT/INSERT/UPDATE/DELETE/MERGE each check the op's effective bit before executing; `system.*` exempt | `ace_exports.cpp` (`sql_acl_effective_ops` callers) |
| Metadata visibility: zero effective bits on an object hides its metadata (`dd_can_view_object_metadata`, consulted by ~15 catalog/DD-info surfaces) | `ace_exports.cpp` |
| GRANT / REVOKE SQL builds bitmasks and enforces immediately | `ace_exports.cpp` + tests |
| `adssys` / `DB:Admin` bypass; `check_rights = 0` bypass | tested |
| No-ACL default = open access (matches ADS) | tested |

### Column-level — model, import, and the *simple-SELECT* half of enforcement

Already present (RCB 07/16/2026 work), also tested:

- **Model**: `PermissionEntry.parent` carries the column's table;
  `grant_column_permission()`; `permitted_columns(user, table, op_bit)`
  returns `nullopt` (no restriction) or the *only* permitted set —
  **fail-secure**: with any column grant present for the user's op, columns
  not in the set are hidden even though table-level access exists.
  `has_any_column_acl()` is the fast opt-out.
- **Import**: `tools/import_dd` step 5c3 reads SAP's own decode
  (`system.permissions WHERE Object_Type = 4`, per-op bit columns) and writes
  native column grants. The mp corpus imports 10; re-verify pmsys' count on
  next import (the old "310 never imported" figure predates this step).
- **Enforcement, simple single-table SELECT path only**
  (`exec_sql_direct_impl`, search `allowed_cols`):
  - `SELECT *` → cursor projection masked to permitted columns;
  - explicit forbidden column in the projection → denied (AQE 7200 envelope);
  - the materialised branch (`TOP` / `ORDER BY` temp) masks the temp's
    columns too.
  - Test: `abi_dd_perms_test.cpp` "Perms: column-level SELECT enforcement".

## 2. The gaps (what "enforcement" still leaks)

The enforcement comment in the code says it plainly: *"Only reached for
simple single-table SELECTs; join/aggregate queries take the materialisation
path above."* Everything below bypasses column ACLs today:

| # | Gap | Leak class |
|---|---|---|
| G1 | `WHERE` on a forbidden column (`SELECT RENT FROM t WHERE DEPOSIT > 1000`) | infers hidden values |
| G2 | `ORDER BY` / `GROUP BY` / `DISTINCT` on a forbidden column | infers ordering/grouping of hidden values |
| G3 | Aggregates over a forbidden column (`SUM(DEPOSIT)`, `SUM(a*DEPOSIT)`) | reads hidden values outright |
| G4 | Joins (2-table, N-way): no `allowed_cols` computed at all — every merged column exposed; join keys unchecked | full bypass |
| G5 | CASE / scalar-fn / window / script paths: same — no check | full bypass |
| G6 | Column-level **writes**: `UPDATE t SET forbidden = …`, `INSERT INTO t (forbidden) VALUES …` (the import already captures per-column UPDATE/INSERT bits; nothing consumes them) | writes hidden columns |
| G7 | **Navigational ABI**: `AdsOpenTable` + `AdsGetString/GetField/SetString` on a restricted table exposes every column (table-level open passes, then no per-column check) | full bypass for ISAM clients (rddads, DA-Web table browse) |
| G8 | Metadata: `system.columns` / `AdsGetFieldName` enumeration lists forbidden columns (SAP behaviour unprobed) | name disclosure |
| G9 | Views / procs reading forbidden columns on behalf of a restricted user (definer- vs invoker-rights — SAP semantics unknown) | indirect read |
| G10 | Remote (serverd) sessions: server executes with the session's user through the same ABI — expected to inherit whatever the local paths do, but unverified | same as G1–G8 remotely |

## 3. SAP semantics to probe FIRST (Phase 0)

Every behavioural choice below must be oracle-probed, not guessed — the
date-format and width work both showed the "obvious" model wrong. Scratch
DD + restricted user against `F:\ads11\ace64.dll` (same harness as
`date_probe` / `wprobe`; connect as the restricted user, not adssys):

- P1: `WHERE forbidden = x` → error (which code?) or silently allowed?
- P2: `ORDER BY` / `GROUP BY` / `DISTINCT` on forbidden → ?
- P3: `SUM(forbidden)` / `SUM(a * forbidden)` → ?
- P4: join ON a forbidden key; `SELECT *` over a join with one side
  column-restricted → masked like single-table?
- P5: `UPDATE SET forbidden`, `INSERT (forbidden)` explicit + `INSERT`
  without column list touching a forbidden column → error codes.
- P6: navigational: `AdsGetString(forbidden)` on an open cursor → 7079?
  5063? value? `AdsGetNumFields`/`AdsGetFieldName` enumeration —
  masked or full?
- P7: `system.columns` for the restricted user → rows filtered?
- P8: a VIEW selecting forbidden columns, executed by the restricted user.
- P9: `VERIFY_ACCESS_RIGHTS` DB property (104) = false → does SAP skip
  column checks even with grants present? (Table-level analogue too.)
- P10: which error code/text for each deny — the AQE envelope must carry
  SAP's exact native code (cf. 2121/2124/5063 precedents).

Record the probe matrix in this file when run.

## 4. Design

### Principle: one chokepoint, not eleven patches

The SELECT engine resolves every column reference through a small number of
sites (projection build, WHERE compile, ORDER BY compile, GROUP BY resolve,
aggregate slot build, join key resolve). Each already produces a
field-index; the plan adds a **single helper** consulted at those sites:

```cpp
// scriptbridge — one call per resolved column reference.
// Returns ok / AE_ACCESS_DENIED per the Phase-0 probe matrix.
UNSIGNED32 acl_check_column(Connection* c, const std::string& table_alias,
                            const std::string& column, uint32_t op_bit);
```

plus the existing `permitted_columns()` for the masking (SELECT *) cases.
Per-statement caching: resolve `permitted_columns` once per (table, op)
into the statement context, as `allowed_cols` already does — the
`has_any_column_acl()` opt-out keeps the no-column-ACL case at zero cost.

### Phases (each lands independently, gates + suite green between)

- **Phase 0 — probe matrix** (½ day): §3 against SAP; record here.
- **Phase 1 — SELECT completeness** (the big one): wire the chokepoint
  into WHERE/ORDER/GROUP/DISTINCT compiles, aggregate slot builds
  (scalar/grouped/join/jgrp/N-way — the slot-build sites are already
  uniform after the 2026-08 aggregate work), and the join paths
  (merged-schema build masks per-side columns; `jcol_index` denies).
  CASE/fn/window path resolves through the same helper.
- **Phase 2 — writes**: UPDATE SET list, INSERT column list (and the
  no-column-list INSERT once that lands), against per-column
  UPDATE/INSERT bits. RI cascades and triggers run as definer (SAP
  precedent expected from P8 — confirm).
- **Phase 3 — navigational ABI**: on `AdsOpenTable` by a column-restricted
  user, install a **cursor projection** of the permitted columns (the
  `cursor_projections()` mechanism from the naming work does exactly this
  shape already) so GetField/GetString/FieldName/NumFields see the masked
  schema; `AdsSetString` etc. deny per-column on write bits. Honour the
  P6-probed enumeration semantics.
- **Phase 4 — metadata**: `system.columns` filtering per P7;
  `dd_can_view_object_metadata` stays object-level, add the column filter
  where SAP has one.
- **Phase 5 — remote + DA-Web smoke**: verify serverd sessions inherit
  everything (they route through the same ABI with the session's user);
  smoke DA-Web panels as a restricted user (rule: test against Aquarium,
  never real DDs).

### Non-goals / guardrails

- `adssys` + `DB:Admin` bypass unchanged everywhere.
- Fail-secure only where SAP is fail-secure (probe first); OpenADS must not
  invent stricter behaviour that breaks legacy apps SAP allowed.
- **Escape hatch before any hard lockout ships** (standing project rule):
  the adssys bypass covers recovery, but Phase 3 must land behind a
  verified "admin can always open everything" test, and the plan's first
  shipped phase must include a documented recovery path in case an
  imported ACL over-restricts (e.g. `import_dd --no-column-perms` flag).
- Parity gates run as adssys and must stay byte-identical through every
  phase.

## 5. Testing

- Extend `abi_dd_perms_test.cpp` per phase (the existing column test is the
  template — scratch DD, restricted `bob`, per-case assertions), each
  verified to fail with its phase reverted.
- One gate-style probe script comparing SAP vs OA as the *restricted* user
  on a scratch DD (the existing gates connect as adssys and cannot see ACL
  behaviour).
- mp/pmsys: re-run `import_dd`, record the imported column-grant counts
  here, and spot-check one known SAP restriction end-to-end.
