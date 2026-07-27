# OpenADS ↔ SAP Parity — Open Items & Verification Backlog

Living checklist of SAP-parity gaps. The S4 gate (`tools/qa-diff/s4_parity.ps1`)
is **42/42** on pmsys as of 2026-07-26. This file tracks what's left and what
still needs verification against current code.

## ⚠️ Blocked on the user
- [ ] **Find other UNENCRYPTED SAP dictionaries for data-level testing.**
      This is on Reinaldo. mp10 (`e:\AdsData\sfi\mp.add`) is sealed by SAP's
      proprietary table encryption — OpenADS can't read the row data
      (COUNT works off the header; field names/data come back as ciphertext),
      so it cannot serve as a data-level second corpus. The S4 gate is
      pmsys-only until an unencrypted DD with real data is available. A second
      corpus is the single biggest thing that would harden the "1:1" claim
      against SQL shapes pmsys never exercises.

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
- [ ] **`system.permissions` row count (zero-row matrix)** — CONFIRMED and
      LARGER than the 53-day memory. pmsys: SAP total **7912** vs OA **2393**;
      Grantee='General' SAP **344** vs OA **141**. SAP emits a row for every
      grantee×object pair (zeros where no ACL); OA emits only actual ACL rows,
      and also emits multiple/field-level rows per object. Import-time bitmask
      VALUES are likely correct now (ACE `--sap-lib` cross-check at conversion),
      but that needs its own clean verification pass — a bracketed value
      compare was blocked by reserved-word/parse quirks this check. Task #5.
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
