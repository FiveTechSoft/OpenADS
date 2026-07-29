# OpenADS ↔ SAP Parity — Open Items & Verification Backlog

Living checklist of SAP-parity gaps. The S4 gate (`tools/qa-diff/s4_parity.ps1`)
is **42/42** on pmsys as of 2026-07-26. This file tracks what's left and what
still needs verification against current code.

## ✅ UNBLOCKED 2026-07-29 — mp10 is now a live second corpus
mp10 was sealed by SAP table encryption. All 90 encrypted tables were decrypted
in place with `tools/decrypt_dd` (SAP's own `sp_DecryptTable`), so the row data
is readable and mp10 now serves as the data-level second corpus this file has
been asking for. Gate: `tools/qa-diff/s4_parity_mp.ps1`.

**Corpus profile** — medical billing, complementary to pmsys in every axis that
matters: 95 tables / 1081 columns / **9.65M rows**; mixed **ADT (90) + DBF with
NTX/CDX (5)** where pmsys is ADT-only; **63801 permission rows** (pmsys 7912);
32 triggers; 11 RI rules; 7 script functions using MERGE / CASE / cursor loops /
TimeStampAdd; 2 views; 3 procs.

**First run: 27 identical, 18 diverged.** Every divergence below is a real
OpenADS gap (test-case bugs already fixed). Grouped by root cause:

- [ ] **Deleted-record visibility on DBF** *(new class, 2 cases)* — SAP counts
      deleted rows (`SET DELETED OFF` default), OpenADS filters them.
      `users.dbf` 18 physical / 8 deleted → SAP 18, OA 10; `provider.dbf`
      20/4 → SAP 20, OA 16; `Forms.dbf` 64/0 → both 64 (control). Invisible on
      pmsys because it is ADT-only. Affects every DBF table with deletions.
- [ ] **SQL feature gaps** *(5 cases)* — `Length()` raises 2158 (unknown
      function) even on a literal, though it is a documented SAP scalar;
      `COUNT(DISTINCT col)` raises 2115; an aggregate over an *expression*
      (`Sum(Real * UNITS)`) raises 2115 — only bare column args parse;
      `SELECT ... FROM <view>` raises 5004 for both mp views, so plain
      single-table view resolution is broken (not just views-inside-joins).
- [ ] **Join column resolution + result naming** *(3 cases)* — a 2-table join on
      `ADM_NUM` returns "Column not found: ADM_NUM"; 3-way and LEFT joins return
      right-side columns renamed `R_UNITS` / `R_Status` and left-side names
      lowercased, where SAP preserves the declared casing.
- [ ] **Numeric / date string formatting** *(3 cases)* — SAP pads and formats to
      the declared field width (`"                 0.00"`, `"  0"`) while OA
      returns `"0"`; date columns inside aggregates come back `"0"` from OA vs
      `"01/18/0203"` from SAP. Same family as task #1 below, now with a second
      corpus confirming it.
- [ ] **DD catalog divergence** — fully diagnosed below. Trigger ordering also
      differs under `ORDER BY Trig_TableName, Name`.

### system.permissions divergence — DIAGNOSED 2026-07-29, and it is small

The first mp run looked alarming (`Object_Type = 4`: SAP 255 vs OA 54050). That
was **a SAP bug, not an OpenADS one**, and the real gap is three concrete items.

**SAP AQE bug — `Object_Type = 4` on system.permissions.** SAP returns 255 while
its own `GROUP BY Object_Type` reports 55131 for the same predicate; `= 1`
(4896) and `= 8` (1632) are both correct, and the semantically identical
`>= 4 AND <= 4` returns the correct 55131. One literal, wrong answer. The gate
uses the range form so the case measures OpenADS; `cat_perms_type4_eq` keeps the
buggy shape as a documented known-diff. **Do not "fix" OpenADS to match 255.**

**The actual divergence reconciles exactly:**

```
SAP: 51 grantees x 1251 objects = 63801   OK
OA : 50 grantees x 1249 objects = 62450   OK
delta 1351 = 1 missing grantee + 2 missing objects
```

- [ ] **`DB:Debug` is not imported** — missing both as a grantee and as a
      type-9 (USER GROUP) object; `DB:Admin`/`DB:Backup`/`DB:Public` all import
      fine. Accounts for the missing grantee *and* one of the two objects.
      Ref: memory `project_sap_builtin_groups.md` (per-user cipher detection).
- [ ] **`Ver8L` link object is not imported** — type 12 (LINK): SAP has
      `Ver8L` + the `LINK` root singleton, OA has only the root. `system.links`
      counts 1 on both, so the link is known but not surfaced as a perm object.
- [ ] **Importer lowercases user grantee names** — **27** mixed-case users, not
      the "two" previously recorded (`RCB`→`rcb`, `AutoTasks`→`autotasks`,
      `PteConfirmations`→`pteconfirmations`, …). Group names are preserved.
      A SAP-compatible client doing `WHERE Grantee = 'RCB'` gets nothing.

**Good news for column-level enforcement:** type 4 is **1081 columns on both**
engines (55131/51 = 54050/50 = 1081), matching `system.columns`. The column
dimension of the matrix is intact — the only shortfall is the missing grantee.

- [ ] **Task #4 missed the user/group catalogs** — same class as the five
      catalogs already fixed, three more to go. `WHERE Name = …` raises
      "Column not found" against all three.
      | catalog | OA columns | SAP columns |
      |---|---|---|
      | users | `USER_NAME` | Name, Enable_Internet, Logins_Disabled, Comment, User_Defined_Prop |
      | usergroups | `GROUP_NAME` | Name, Comment, User_Defined_Prop |
      | usergroupmembers | `GROUP_NAME`,`USER_NAME` | (verify against SAP) |

## A. S4 polish (cosmetic — tracked as tasks #1–3)
- [ ] **#1 Date display format** — `AdsGetString` on date cols returns raw
      `YYYYMMDD`; SAP formats per connection date format (default `MM/DD/YYYY`).
      Highest-value but riskiest (touches every string-read path). FIRST do a
      DA-Web/OpenERP impact check — do those apps depend on raw `YYYYMMDD`?
      Ref: memory `project_oa_date_string_gap.md`.
- [ ] **#2 `_spout_` >10-char output column names** — proc `__output` temp is a
      DBF free table, so `databasepath` → `databasepa`. Fix: make `__output`
      an ADT-typed temp (long field names). Low risk.
- [ ] **#3 EXPR_n numbering, mixed aliased/unaliased aggregates** — verify
      `SELECT COUNT(*), SUM(x) AS s, MIN(y)` column naming matches SAP.

## B. VERIFIED OPEN 2026-07-26 (was "needs verification")
- [ ] **Catalog column-name + column-SET divergence** — CONFIRMED across all
      five DD catalogs on pmsys. OA invents its own names AND omits/renames
      columns; a SAP-compatible client filtering by SAP column names breaks
      (proved: `WHERE Object_Type = ...` on OA → "Column not found:
      Object_Type" because OA's column is `OBJ_TYPE`). Task #4.
      | catalog | OA columns | SAP columns |
      |---|---|---|
      | storedprocedures | PROC_NAME,CONTAINER,PROCEDURE,INPUT,OUTPUT | Name,Proc_Input,Proc_Output,Proc_DLL_Name,Proc_DLL_Function_Name,Comment,Proc_Invoke_Option,SQL_Script |
      | functions | FUNC_NAME,CONTAINER,RET_TYPE,IN_PARAMS,FUNC_BODY,COMMENT | Name,Package,Return Type,Input Parameters,Implementation,Comment,User_Defined_Prop |
      | triggers | TRIG_NAME,TABLE_NAME,EVENT_MASK,TIMING,EVENT,CONTAINER,PROC,PRIORITY,ENABLED,TRIG_OPTIONS | Name,Trig_TableName,Trig_Event_Type,Trig_Trigger_Type,Trig_Container_Type,Trig_Container,Trig_Function_Name,Trig_Priority,Trig_Options,Comment,Trigger |
      | relations | RI_NAME,PARENT,CHILD,PARENT_TAG,CHILD_TAG,UPDATE_OPT,DELETE_OPT,FAIL_TABLE | Name,RI_Primary_Table,RI_Primary_Index,RI_Foreign_Table,RI_Foreign_Index,RI_UpdateRule,RI_DeleteRule,RI_No_PKey_Error,RI_Cascade_Error |
      | permissions | OBJ_NAME,OBJ_TYPE,PARENT,GRANTEE,SELECT..DROP | Name,Object_Type,Parent,Grantee,Select..Drop |
      NOTE: `system.indexes` was already fixed to SAP names — same fix pattern.
- [x] **`system.permissions` row-matrix parity** — DONE 2026-07-28. Rewrote the
      builder in `ace_exports.cpp` to emit SAP's full uniform grantee×object
      cross product. pmsys now **7912 rows = 23 grantees × 344 objects**, exactly
      matching SAP; per-type breakdown identical (t1=33, t4=262, t6=1, t8=9,
      t9=16, t10=8, t11=1, t12=1, t15=1, t17=1, t18=10, t19=1). The 344 objects =
      all tables + every column (read live from physical descriptors, like
      system.columns) + users/groups/procs/functions + per-category root
      singletons (`TABLE`/`VIEW`/`USER`/`USER GROUP`/`PROCEDURE`/`LINK`/
      `PUBLICATION`/`SUBSCRIPTION`/`PACKAGE`/`Database`); grantees = real users +
      groups + server pseudo-groups `SERVER:Admin`/`SERVER:Monitor`, adssys
      omitted. Cells always render `0`/`1`/`2`, never blank (matches SAP). VALUES
      verified byte-identical for the common cases — e.g. `General/landlords`
      returns S=U=I=D=1, rest 0, exactly as SAP; procs return Execute=1. The
      encrypted-blob ceiling only affects user-direct grants (render 0). All 10
      permissions unit tests pass; S4 gate 40/42 (2 fails = #138 lock regression).
      FOLLOW-UP (minor, import-casing): OA surfaces two grantees lowercased —
      `autotasks`/`rcb` where SAP has `AutoTasks`/`RCB`. Same entities, cosmetic;
      the importer lowercases these user names. Not a row-matrix issue.
- [ ] **Views inside joins** — single-table view resolution was prototyped
      (then reverted with the mp10 work); a view used *within* a join is not
      resolved. Verify + decide scope. Not yet tasked.

## C. Larger open work (not SQL-parity blocking)
- [ ] **ACE64 export surface** — 393/578 SAP exports present, 231 missing
      (memory 22 days old, `project_ace64_compat.md`). Binary-ABI clients only.
      Report: `ACE64_API_COMPAT_REPORT.local.md`.
- [ ] **VFP table support** (DBF 0x30/0x31) — `table.cpp` errors on VFP-typed
      DBF; needs `_NullFlags` bitmap + VFP autoinc. Deferred from M4.
- [ ] **Forward-only prefetch (M12.21)** — disabled after cursor-drift
      regressions on indexed scans; re-enable once drift root cause understood.

## D. Out of scope / indefinitely deferred
- ADS proprietary ADT **table encryption** (format not reversed) and
  `AdsDecryptTable`/`EncryptRecord`/`DecryptRecord` — stub
  `AE_FUNCTION_NOT_AVAILABLE`. The per-object encrypted permission blobs are
  the same problem (worked around at import via the ACE cross-check).

## Done this session (do NOT reopen — older memory still lists these as gaps)
UPDATE `SET col = expr` RHS · `EXECUTE IMMEDIATE` · `DECLARE CURSOR AS EXECUTE
PROCEDURE` · trigger error propagation · nested UDF calls · bracketed column
identifiers · proc/UDF body truncation · `sp_GetPhysicalPath` · joins
(2-table + N-way, ADT + DBF, aggregates, CICHAR, DISTINCT/TOP/LIMIT) ·
GROUP BY key projection · SAP AVG semantics · AQE 7200 error envelope ·
EXECUTE PROCEDURE 2124 arg-binding type check · CICHAR index seeks ·
2137 ambiguous-column strictness. The 9-day `project_pmsys_exec_scope.md`
memory is largely superseded.
