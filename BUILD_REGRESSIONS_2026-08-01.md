# Failing tests on `main` — bisected report, 2026-08-01

`main` (`4a610e86`, v1.8.46) fails **11 unit-test cases**. Both clusters bisect
to two commits in the 30 Jul batch. Nothing before that batch is implicated:
its parent `4fc5b66c` passes every affected test.

All eight commits in the batch are authored by
**MiMo V2.5 Free `<mimo@opencode.ai>`**; `35c3bd62` was *committed* by
**Antonio Linares `<alinares@fivetechsoft.com>`**.

## Bisection

| commit | subject | ADT-scope | CDX | savepoint/rollback |
|---|---|---|---|---|
| `4fc5b66c` | *(parent of the batch)* | ✅ pass | ✅ pass | ✅ pass |
| `234d42d6` | fix(remote): maintain CDX bags on APPEND via ABI twin handle | ❌ **fail** | ✅ pass | ✅ pass |
| `57bef9d5` | fix(engine): multiuser browse sees peer-appended rows | ❌ fail | ✅ pass | ✅ pass |
| `e7d45f41` | perf(engine): lazy multiuser refresh on Skip | ❌ fail | ✅ pass | ✅ pass |
| `35c3bd62` | **perf(engine): dirty-record write coalescing** | ❌ fail | ❌ **fail** | ❌ **fail** |
| `44852ddb` … `4a610e86` | four remote fixes | partly fixed | ❌ fail | ❌ fail |

Totals: `4fc5b66c` 0 failures → `e7d45f41` 5 → `35c3bd62` and later 11.
The four newest commits *repaired* three of the earlier remote failures
(`abi_remote_prefetch_test`, `abi_remote_close_hang_test`), so the batch is a
net −3 on remote and +6 on engine.

---

## Problem 1 — `35c3bd62` breaks transaction rollback (data loss)

**Author:** MiMo V2.5 Free `<mimo@opencode.ai>` · **Committer:** Antonio Linares
**Commit:** `perf(engine): dirty-record write coalescing for append/replace/commit` (v1.8.43)
**Touches:** `src/engine/table.cpp` (+117/−), `src/engine/table.h`, `src/abi/ace_exports.cpp`

Field setters now encode into the record buffer and defer writeback until
WriteRecord / flush / navigation / unlock. Rollback no longer restores the
pre-transaction image.

**Failing cases (6):**

| test | assertion |
|---|---|
| `abi_savepoint_test.cpp:90` | partial rollback to savepoint → got `BBBBB`, want `BBBBB` restored state |
| `abi_savepoint_test.cpp:105` | same case, second field |
| `abi_savepoint_test.cpp:140` | `AdsRollbackTransaction80(null)` full rollback → got `BBBBB`, want `AAAAA` |
| `abi_savepoint_test.cpp:277` | nested BEGIN + ROLLBACK → got `BBBBB`, want `AAAAA` |
| `abi_m5_smoke_test.cpp:154` | BeginTransaction + update + Rollback → original record not restored |
| `abi_m5_smoke_test.cpp:197` | AppendRecord in tx + Rollback → `AdsGotoTop` fails outright |

**Why it matters:** a rollback that leaves the modified value on disk is silent
data corruption. This has shipped in v1.8.43 → v1.8.46.

**Likely cause:** the coalescing path snapshots the record buffer for writeback
but the transaction layer captures its undo image at a point that now sees the
already-mutated buffer — or never sees a write at all, so there is nothing to
undo.

---

## Problem 2 — `35c3bd62` breaks CDX index maintenance

Same commit. Deferring index sync to WriteRecord/flush leaves tags stale or
double-written.

**Failing cases (3):**

| test | assertion |
|---|---|
| `abi_cdx_multitag_create_test.cpp:319` | `CreateIndex61` composite expr + multiple FOR → `cnt == 2` fails |
| `abi_cdx_recreate_tag_diff_expr_test.cpp:109` | re-created tag does not follow the new column; ascending walk ≠ `{1,2,3}` |
| `abi_cdx_recreate_tag_diff_expr_test.cpp:132` | same, descending walk ≠ `{4,3,2,1}` |
| `abi_ordnumber_test.cpp:135,136` | multi-tag appends double-write the index; `kc1`/`kc2 != N` |

**Verified:** `e7d45f41` passes all 75 CDX cases; `35c3bd62` fails 3.

---

## Problem 3 — `35c3bd62` breaks AFTER-trigger propagation

Same commit.

| test | assertion |
|---|---|
| `abi_script_proc_test.cpp:266` | failing AFTER trigger on SQL UPDATE → `SELECT ID FROM orders` returns wrong value; the write survives when the trigger should have blocked it |

Consistent with problems 1–2: the write is already coalesced into the buffer by
the time the trigger rejects it, so the rejection cannot undo it.

---

## Problem 4 — `234d42d6` breaks remote ADT numeric round-trip

**Author:** MiMo V2.5 Free `<mimo@opencode.ai>`
**Commit:** `fix(remote): maintain CDX bags on APPEND via ABI twin handle`
**Touches:** routes Append/SetField/Delete through the "ABI twin" handle

| test | assertion |
|---|---|
| `abi_adt_scope_validation_test.cpp:348` | remote ADT: `AdsSetDouble(99.0)` then `AdsGetDouble` → `q != 99.0` |

At `234d42d6` this file failed 2 cases; the four newest commits repaired one.
One remains at HEAD.

**Note:** this is *not* related to the ADT decimal-count fix in `c0ba4955` —
verified by running the same test with and without that commit.

---

## Reproduce

```bash
cmake --build build/ninja-clang-local --target openads_unit_tests
build/ninja-clang-local/tests/openads_unit_tests.exe \
    --test-case="*avepoint*,*ollback*,*CDX*,*ADT scope*"
```

Baseline for comparison:

```bash
git checkout 4fc5b66c   # parent of the batch — all of the above pass
```

## Suggested order

1. **`35c3bd62` rollback** — data loss, shipped in four tags. Either fix the
   undo-image capture or revert the coalescing until it is transaction-safe.
2. **`35c3bd62` CDX + trigger** — same commit, same root shape; likely fixed
   together with 1.
3. **`234d42d6` remote ADT numeric** — lower severity, isolated to one case.

## Ownership summary

| problem | commit | author | committer |
|---|---|---|---|
| rollback / CDX / trigger (10 cases) | `35c3bd62` | MiMo V2.5 Free `<mimo@opencode.ai>` | Antonio Linares `<alinares@fivetechsoft.com>` |
| remote ADT numeric (1 case) | `234d42d6` | MiMo V2.5 Free `<mimo@opencode.ai>` | MiMo V2.5 Free `<mimo@opencode.ai>` |
