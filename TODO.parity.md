# OpenADS ↔ SAP Parity — Open Items & Verification Backlog

Living checklist of SAP-parity gaps. Two S4 gates now run:
`tools/qa-diff/s4_parity.ps1` (pmsys, 41/1 — the 1 is sandbox counter drift, not
a defect) and `tools/qa-diff/s4_parity_mp.ps1` (mp, 32/23 as of 2026-08-05).
This file tracks what's left and what still needs verification against
current code.

---

## 🔖 TRACKED BACKLOG — verified open, ready to pick up (2026-07-30)

Five items confirmed against current `main`, each with the exact site and the
shape of the fix, so none of them gets lost. Ordered by value.

| # | Item | Where | Verified |
|---|---|---|---|
| 1 | **Column-level permission ENFORCEMENT** | `get_effective_ops()` / query projection | analysed, not started |
| 2 | ~~`sp_` `__output` truncates column names to 10 chars~~ | `run_dd_procedure()` | ✅ **FIXED 2026-07-30** |
| 3 | ~~`AdsSetString` into an ADT DOUBLE stores text verbatim~~ | ADT write path | ✅ **ALREADY FIXED** (remote-twin encode work); pinned by test 2026-08-06 |
| 4 | **`DB:Debug` group not imported** | `import_dd` | ✅ mp gate `cat_db_groups` |
| 5 | **`Ver8L` link not surfaced as a permission object** | permissions matrix builder | ✅ mp gate `cat_perms_links` |

### 1. Column-level permission enforcement — the security item

**PLANNED — see `docs/plans/column-level-permissions.md`** (2026-08-05
survey; implementation deferred behind full SQL-engine parity).

The old summary here was stale on both counts. Actual state: table-level
(file-level) enforcement is DONE and covered by ~20 test cases (open gate,
SQL DML/SELECT gates, metadata visibility, GRANT/REVOKE, groups, adssys
bypass). The column-level MODEL, the import (`import_dd` step 5c3 reads
SAP's own `Object_Type = 4` decode), and the *simple single-table SELECT*
enforcement (projection masking + explicit-column deny, tested) also
exist. What still bypasses column ACLs: WHERE/ORDER/GROUP on forbidden
columns, aggregates, ALL join paths, CASE/fn/window, column-level writes,
the navigational ABI, and column metadata — the plan's gap table G1–G10,
with the SAP probe matrix to run first.

### 2. ✅ FIXED 2026-07-30 — `sp_` `__output` no longer truncates to 10 chars

Was: `EXECUTE PROCEDURE sp_GetPhysicalPath()` returned `databasepa` where SAP
returns `databasepath`. Capping every stored-procedure result column at 10
characters limited the SQL engine for no reason — procedures read ADT tables and
declare outputs mirroring SAP catalog columns, whose names run to 18.

The earlier v1.8.40 fix covered only the **static-cursor** path (`_srt_`);
procedure output is a separate site. `run_dd_procedure()` builds `_spout_*`
through `CREATE TABLE … AS FREE TABLE`, which takes its format from the
statement's table type and defaults to CDX (DBF). It now pins `ADS_ADT` across
that one DDL and restores the caller's setting afterwards, so a procedure body
running its own `CREATE TABLE` does not inherit the choice.

Also lifted `sql_type_of()`'s `CHAR` clamp from 254 (DBF's character maximum) —
ADT carries the length in a uint16, and an over-long *record* is now rejected by
`AdsCreateTable` rather than silently shortened. SAP procs routinely declare
`CICHAR(255)` and wider.

Guarded by `abi_script_proc_test.cpp` *"script proc: `__output` keeps column
names longer than 10 chars"*, verified to fail with the fix reverted. mp gate
`sp_GetPhysicalPath` now passes on the column name, not just the path.

**✅ FIXED 2026-08-03 — the join / union / aggregate materialisers no longer
truncate.** All seven (`_join_`, `_mjoin_`, `_uni_`, `_agg_`, `_grp_`,
`_jagg_`, `_jgrp_`)
now share `materialise_temp_adt()`, which builds an ADT temp with the callers'
cell bytes verbatim; the merged `R_<name>` spelling is untruncated, the N-way
join no longer dedups two long names agreeing in their first 10 chars, and
group-key columns take their `AS` alias / written spelling like SAP
(`Insurance AS Insurance_Carrier_Group` byte-matches now). Temps are also
deleted when their cursor closes — the DBF-era paths leaked one per query.
Guarded by `abi_sql_temp_names_test.cpp` (fails 17 assertions with the fix
reverted); doc: `docs/materialised-cursor-temps.md`. **2026-08-05: the last
three (`_scr_`, `_call_`, `_case_`) converted too — every SQL result
materialiser is now ADT.** CASE/window/fn aliases keep full names (were cut
at 11); `_scr_`/`_call_` temps now delete on close.

### ✅ Numeric presentation in joins + aggregates — FIXED 2026-07-30

Separate defect from the truncation, found while starting on the materialisers.
Values are now byte-identical to SAP.

- [x] **ADT decimal count read from the wrong byte.** `adt_driver.cpp` read the
      scale at 137; SAP writes it at **139**. Verified byte-for-byte against
      `service.adt`: an N(11,2) column reads `…137:0 138:0 139:2`. Every
      SAP-written ADT table reported `Field_Decimal = 0`. Hidden because ADT
      type 2 stores numerics as ASCII — a plain read passes the stored text
      through and never consults `decimals`, so only re-formatting paths broke.
      Reader now prefers 139 (type-2 only, so an AUTOINC counter at 139 is never
      mistaken for a scale) and falls back to 137 for tables written by older
      OpenADS builds; the writer emits both.
- [x] **Join cells were left-justified.** `join_cell_text()` now right-justifies
      to the declared width, so a joined column renders exactly as a plain read
      of it (`"      0.00"`, not `"0.00"`).
- [x] **Aggregate widths were hardcoded to 20** across all five materialisers.
      SAP's rule, oracle-probed: `SUM` → source + 10; `AVG`/`MIN`/`MAX` → source
      width; `COUNT` → integral and **unpadded**. Now in `agg_result_width()` /
      `agg_cell_text()` so the five paths share one definition.

mp gate 29 → 30; `agg_sum_payfile` now passes and `agg_mixed_aggs`'s COUNT and
SUM are byte-identical to SAP.

Still open in this area, each a *different* defect from the formatting:

- [x] **`MIN`/`MAX` over a DATE column — FIXED 2026-08-02.** The accumulators
      were all `double`, so a date (and a character column) contributed
      `as_double == 0` for every row and the aggregate answered a question about
      dates with a number. MIN/MAX over a non-numeric source now compare the
      decoded text — dates decode to `YYYYMMDD`, which orders lexicographically
      exactly as it does chronologically — and the result column is CHARACTER
      sized to the widest decoded value rather than the source's on-disk width.
      `MIN(PAY_DATE)`/`MAX` now return `02030118`/`20170320`, the same dates SAP
      shows as `01/18/0203`/`03/20/2017`; the remaining difference is purely the
      display format (task #1), which affects every date read, not aggregates.
      `MIN`/`MAX` over a character column now match SAP byte-for-byte
      (`ABAD`/`ZZ TESTPATIENT`) — that was silently broken the same way.
      **Extended 2026-08-02 to all four aggregate materialisers** — scalar,
      single-table GROUP BY, join + GROUP BY, and join scalar — sharing one
      classifier (`agg_source_is_text`) and one accumulator (`TextMinMax`) so
      they cannot drift apart. For the grouped paths the result column is
      sized from the widest decoded value across *every* group, since a column
      has one width for the whole cursor. A blank value is treated as absent
      rather than as a minimum: a zero-JDN ADT date is NULL and decodes to "",
      but a join/group temp stores it as a blank cell with `is_null` unset, so
      without that guard an empty string won every MIN — which is exactly how
      the join paths first surfaced `""` instead of a date. SAP agrees: its MIN
      over a column with blank dates returns the earliest real date.
- [x] **ADT non-character columns truncated through a materialised cursor —
      FIXED 2026-08-02.** `type_name()` in the static-cursor materialiser
      switched only on DBF *letter* type codes, but an ADT descriptor carries a
      NUMERIC type code (1–22) in the same field, so every non-character ADT
      column fell through to `Character` with its ON-DISK byte width. An ADT
      date (type 3, 4 bytes) became `CHAR(4)`, so `TOP 1 ADM_DATE` returned
      `"2009"` instead of `"20090816"` — while the same column read correctly
      without `TOP`. Numerics survived only by luck, ADT type 2 being ASCII
      already. Now mirrors the CTAS path, which always had both switch blocks.
- [x] **Aggregate over an *expression* — FIXED 2026-08-02.** The parser
      accepted only `*` or a bare identifier inside an aggregate, so
      `Sum([real] * units)` hit "expected ')' to close aggregate" (2115). An
      aggregate argument may now be `a <op> b` (column or literal RHS), reusing
      the same shape and arithmetic as a projection-level `$ARITH_` item, so
      only the accumulation differs. The result's SCALE follows the operation:
      multiplication adds the operand scales — SAP renders
      `SUM(Real * PRICE)` with 4 decimals for two `N(..,2)` columns — addition
      keeps the wider one. Values now match SAP on `SUM(Real * units)`,
      `SUM(UNITS * UNITS)` and `SUM(Real)`.
- [x] **`COUNT(DISTINCT col)` — FIXED 2026-08-02.** Parser accepts the
      `DISTINCT` keyword; the accumulator keeps a per-slot set of decoded
      values. A blank IS a distinct value to SAP — `COUNT(DISTINCT insurance)`
      over mp returns 58 where only 57 are non-blank — so only NULL is
      excluded. Byte-identical to SAP.

- [x] **GROUP BY does not complete on a large table — FIXED 2026-08-04.**
      Root cause was nowhere near the grouped walk: `snapshot_ri_pks` runs on
      EVERY navigation, and on a DD connection it re-resolved the table's DD
      alias each time — `ri_alias_for_path` called `fs::weakly_canonical`
      (a filesystem syscall) once per DD table per call, ~7 ms per `AdsSkip`
      on the 95-table mp DD. The engine built all 382K groups in FOUR seconds;
      the "hang" was the CLIENT's row loop at 7 ms/row ≈ 45 minutes. Fixed
      twice over: the alias is cached on the Table (`ri_alias_cached()` —
      the path never changes after open), and `ri_alias_for_path` gained a
      basename pre-filter so non-matching entries (every SQL temp) cost pure
      string compares. Skip went 7,032 µs → 2.8 µs; the full unbounded
      382K-group query now completes in ~7 s end-to-end and is
      **value-identical to SAP on all 381,977 rows** (declared width of the
      arithmetic column still diverges — tracked below).
      **Completing it exposed a value bug the bounded gate case had masked:**
      the grouped, join, join+GROUP-BY and N-way aggregate paths evaluated
      only the bare left column of `SUM(a * b)` — `SUM([real] * units)`
      summed `SUM(real)` (9,779 of 381,977 groups wrong; the gate's group
      happened to have units = 1). All four paths now evaluate the
      expression (the scalar path always did), with the op-following result
      scale. Guarded by *"SUM(a*b) evaluates the expression in grouped and
      join paths"* in `abi_sql_agg_test.cpp`, verified to fail (3 asserts)
      with the fix reverted.

Two residual divergences in this area, both measured, neither guessed at:

- [x] **Declared width of an arithmetic aggregate result — FIXED 2026-08-05.**
      The six original measurements DID fit one rule once recast as an
      (integer-digits, scale) pair per operand; 45 fresh oracle probes (a
      scratch `wprobe` table with varied N(L,s) shapes, literals, integer
      columns, all four operators) pinned it completely — including division,
      whose scale is `s1 + ip2 - 1` and whose `ip` carries an empirical
      `min(s1, 2)` term. Implemented as `scriptbridge::agg_arg_shape()` and
      wired through all five aggregate materialisers; the parser now keeps a
      literal's text as written (`"2.50"` is N(4,2) to SAP, which the parsed
      double cannot say). 39/39 probe expressions byte-identical;
      `agg_inline_sumbyclaim` flipped the gate to 32 identical. Full rules in
      the `agg_arg_shape` comment block.
- [x] **Accumulation precision — FIXED 2026-08-05.** Two distinct causes:
      *division* results can exceed their declared scale, and SAP truncates
      each row's quotient at that scale toward zero BEFORE accumulating
      (probe: quotients 0.4166666… + 0.6666666… sum to `1.0833332`, not
      `...33`) — now mirrored in `agg_arg_value`; and summing 623K
      double-rounded products drifts the display digits
      (`SUM(Real * PRICE)` read `...2047` vs SAP `...2000`) — the five
      accumulate loops now use Kahan-compensated summation. The unbounded
      382K-group query is byte-identical to SAP across all rows INCLUDING
      padding. Found on the way: OA rejects `INSERT INTO t VALUES (...)`
      without a column list (2115) — SAP accepts it; still open, listed in
      section B.
- [x] **Result-column naming** — *done*. Turned out to be wider than the `R_`
      prefix: the select-list-spelling rule applies to **every** SELECT, not
      just joins, and `AS` aliases were being ignored outright on the join
      path. Both the live-cursor and materialised paths now name columns the
      way SAP does; the rule and its three traps are written up in
      `docs/materialised-cursor-temps.md` (constraint 4). What is *not* fixed
      is the `R_` prefix itself — it remains the merged temp's internal
      spelling for a colliding right-side column, now invisible to clients.
      That still matters for constraint 2: `R_` + a long name overflows a DBF
      descriptor, so join/union/aggregate temps moving to ADT is still open.
- [ ] **`ntx_provider`** — `DOCT_NO` reads `"0"` vs SAP `"  0"` on a *plain* DBF
      read (no join, no aggregate), so a separate path from everything above.

### 3. ✅ ALREADY FIXED — `AdsSetString` into an ADT DOUBLE parses the text

Verified 2026-08-06: the remote-twin work taught `encode_field_string` to
parse strings into every ADT/VFP binary numeric type (Double, Integer,
Currency, ShortInt, AutoInc, Time), which covered the local `AdsSetString`
path the moment it landed — this item just never got re-checked. The exact
repro from the original report (ADT `Numeric,12,2` → DOUBLE, AdsSetString
"10.50") now round-trips as 10.50 through both `AdsGetDouble` and
`AdsGetString`. Pinned by *"AdsSetString into an ADT DOUBLE parses the
text"* in `abi_adt_dat_extension_test.cpp`, proven to fail (3 assertions)
with the parse branch removed.


### 4. `DB:Debug` is not imported

Missing both as a grantee and as a type-9 (USER GROUP) object;
`DB:Admin`/`DB:Backup`/`DB:Public` all import fine. It is one of the two items
behind mp's 63801 vs 62450 permission-row gap, and also part of
`system.usergroups` 20 vs 17. Ref: memory `project_sap_builtin_groups.md`
(per-user cipher detection).

### 5. `Ver8L` link is not surfaced as a permission object

Type 12 (LINK): SAP has `Ver8L` plus the `LINK` root singleton, OpenADS has only
the root. `system.links` counts 1 on both, so the DD knows the link — it just
is not projected into the permissions matrix. Second of the two items behind the
row-count gap above.

**Also open, smaller:** `system.usergroups` omits `SERVER:Admin`/`SERVER:Monitor`
that the permissions builder already synthesises (2 of the 3-group shortfall);
`adssys` appears in OpenADS's user catalogs where SAP omits it.

---

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
- [x] **SQL feature gaps — ALL FIVE FIXED.** ~~COUNT(DISTINCT)~~ /
      ~~expression aggregates~~ 2026-08-02; ~~Length()~~ 2026-08-05;
      ~~`SELECT ... FROM <view>`~~ 2026-08-05: views resolve via the
      derived-table machinery, composing outer clauses like SAP
      (`vw_SumByClaim` + `vw_TestView` gate cases byte-identical).
      Fixed with it: the SAP binary View decode had sql/comment SWAPPED
      (every imported view had an empty statement — re-run import_dd to
      repair converted DDs), and the scalar/grouped aggregate walks
      ignored a derived cursor's own filter, so a COUNT over any
      filtered view/derived table counted every row.
- [x] **Join column resolution — FIXED 2026-08-04** (`jcol_index`: merged
      R_ fallback + qualifier strip at every aggregate/GROUP BY/WHERE
      resolution site). The **result naming** half was fixed earlier: `R_UNITS` / `R_Status` and the
      lowercased left-side names are gone, and the rule turned out to apply to
      every SELECT rather than only joins — see the backlog entry above.
- [ ] **Numeric string formatting** *(date half FIXED)* — the date parts
      closed 2026-08-04 (display format) and 2026-08-05 (aggregate widths):
      date columns inside aggregates now render `"01/18/0203"` like SAP.
      Remaining: the plain-DBF-read padding family (`ntx_provider DOCT_NO`
      `"0"` vs `"  0"` — see the item above) and the blank-LOGICAL rendering
      (`""` on SAP vs `"F"` on OA, found 2026-08-04 via `SELECT * FROM
      admit`).
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

- [ ] **`DB:Debug` is not imported** → **backlog item 4** (top of file).
      Missing both as a grantee and as a type-9 (USER GROUP) object;
      `DB:Admin`/`DB:Backup`/`DB:Public` all import fine. Accounts for the
      missing grantee *and* one of the two objects.
- [ ] **`Ver8L` link object is not imported** → **backlog item 5** (top of file).
      Type 12 (LINK); `system.links` counts 1 on both, so the link is known but
      not surfaced as a perm object.
- [x] **Importer lowercased user grantee names — FIXED 2026-07-30** (commit
      `e4f3c055`). 27 mixed-case users, not the "two" previously recorded.
      `users_` keeps folded lookup keys; `user_display_` carries the declared
      spelling and `save()` persists it. Case mismatches vs SAP: 27 → 0, and
      `WHERE Grantee = 'RCB'` returns rows again. Note existing converted DDs
      hold the folded spelling on disk and need a re-import to recover it.

**Good news for column-level enforcement:** type 4 is **1081 columns on both**
engines (55131/51 = 54050/50 = 1081), matching `system.columns`. The column
dimension of the matrix is intact — the only shortfall is the missing grantee.

- [x] **Task #4's three missing catalogs — DONE 2026-07-29.** `system.users`,
      `system.usergroups` and `system.usergroupmembers` now emit SAP's exact
      column names, order and types, so `WHERE Name = …` works. Note
      usergroupmembers was `User_Name` THEN `Group_Name` in SAP — the reverse of
      the pair OpenADS emitted, so positional readers had the values swapped as
      well as misnamed. Both backends updated (native DD + the SQL-URI/sqlite
      ACL projection in `sql_acl_store.cpp`, which had its own copy) plus the
      DA-Web queries that named the old columns explicitly.

**Newly visible now that those columns exist** — the shapes match, the values
don't:

- [ ] **import_dd does not capture user/group `Comment` or `User_Defined_Prop`**
      — SAP has `AM` → "Amneris Maldonado" and `AdjustmentAuthUsers` →
      "Authorize adjustments to claims"; OpenADS returns empty for every user
      and group. Same for the `User_Defined_Prop` XML blob (SAP stores an
      `<EMAIL>` element per user). The DD can hold these (`prop_1` / `prop_3`
      via `set_user_property`) — the importer just never reads them from SAP.
- [ ] **`system.usergroups` omits `SERVER:Admin` / `SERVER:Monitor`** — SAP
      lists both (that is 2 of the 3-group shortfall, 20 vs 17; `DB:Debug`
      above is the third). Inconsistent *within* OpenADS: the
      `system.permissions` builder already synthesises both pseudo-groups, the
      usergroups builder does not. Cheap fix, but it will surface them in
      DA-Web's group tree, so decide that deliberately.
- [ ] **`adssys` appears in OpenADS's user catalogs, SAP omits it** — the
      importer creates it ("adssys created (SAP built-in, not in export)") and
      it sorts first, shifting every ordered comparison. Accounts for users
      31 (SAP) vs 32 (OA).

## ✅ FIXED 2026-07-30 — the 10-char truncation regression on v1.8.40

**`TOP` / `ORDER BY` / `DISTINCT` / `LIMIT` truncated every column name to 10
characters.** Same root cause as task #2 (the materialisation temp was a **DBF**
free table, and DBF caps field names at 10) but a far wider blast radius than
the `sp_` `__output` case that task describes — it was not cosmetic.

**Fix:** the temp is now ADT, and its decimal numerics use ADT type 2 (ASCII
digits) via the new `AsciiNumeric` field-type name, so full-length names and the
declared numeric scale survive together. Three constraints pin that format and
two of them only surface in specific customer scenarios — the reasoning is
written up in **`docs/materialised-cursor-temps.md`**, linked from
CONTRIBUTING.md and from the code at the decision site. Regression test:
`abi_sql_orderby_test.cpp` *"materialised cursors keep column names longer than
10 chars"*, which asserts names and scale in the same pass.

Found while doing it, **not fixed**:

- [x] **`AdsSetString` into an ADT DOUBLE** — already fixed by the
      remote-twin encode work; pinned by test 2026-08-06 (item 3).
- [x] **The other materialisers still truncate to 10 chars** — FIXED: all
      eleven SQL result materialisers converted to ADT (joins/unions/
      aggregates 2026-08-03, `_scr_`/`_call_`/`_case_` 2026-08-05).

Original report, kept for context:

```
SELECT Name, RI_Primary_Table, RI_Foreign_Table FROM system.relations
   -> RI_Primary_Table   (correct)
... the same query + ORDER BY Name
   -> RI_Primary         (truncated)

SELECT TOP 1 AccidentDate, LengthOfNeed FROM admit   -> AccidentDa, LengthOfNe
SELECT DISTINCT AccidentDate FROM admit              -> AccidentDa
```

Affects **user tables**, not just catalogs, so it hits real applications. It also
silently breaks the SAP-parity column names from tasks #4 and the users/groups
work, because SAP's catalog names are routinely longer than 10 chars
(`RI_Primary_Table` 16, `Enable_Internet` 15, `User_Defined_Prop` 17,
`Trig_Function_Name` 18).

Origin: **#136** materialises single-table ORDER BY / DISTINCT / LIMIT through
`build_memory_result()`, a helper written for `system.*` and `sp_*` result sets.
**#146** (91f0ea49) fixed that helper's numeric typing and 64 KB record cap but
not the field-name length. Confirmed as a regression: the `cat_rel_names` mp gate
case passed before the rebase and fails after it.

Fix is the one task #2 already prescribes — materialise into an **ADT** temp
(long field names) instead of a DBF — but it should now be scoped to
`build_memory_result()` generally, not just the proc `__output` path.

## A. S4 polish (cosmetic — tracked as tasks #1–3)
- [x] **#1 Date display format** — **FIXED 2026-08-04**. The premise was
      wrong: probing SAP showed `AdsGetSTRING` returns raw `YYYYMMDD` on SAP
      too — only `AdsGetFIELD` / `AdsGetDate` format (default `MM/DD/CCYY`).
      OA now matches: GetField/GetDate format Date + Timestamp (12-hour,
      2-digit hour), GetString stays raw (php_ads reads exclusively through
      it — zero DA-Web/OpenERP impact, audited), default + SetDateFormat
      normalisation (`YYYY`→`CCYY`) match SAP, `AdsSetDate`/`AdsSetTimeStamp`
      parse per the format (both wrote garbage before), `AdsSetEmpty` on an
      ADT date stored spaces = year 1470954 (now JDN 0), and date-sourced
      MIN/MAX aggregate columns are declared DATE in their temps so they
      format too. mp gate 30→31 (`agg_mixed_aggs`); rules + deliberate
      deviations in `docs/date-display-format.md`; guarded by
      `abi_date_format_test.cpp`. Found while probing: SAP renders a blank
      LOGICAL as `""` where OA says `"F"` (`SELECT * FROM admit` isClosed) —
      separate small family, still open.
- [x] **#2 `_spout_` >10-char output column names** — FIXED 2026-07-30
      (section 2 above); the remaining materialisers followed 2026-08-03/05.
- [ ] **#3 EXPR_n numbering, mixed aliased/unaliased aggregates** — verify
      `SELECT COUNT(*), SUM(x) AS s, MIN(y)` column naming matches SAP.

## B. VERIFIED OPEN 2026-07-26 (was "needs verification")
- [x] **`INSERT INTO t VALUES (...)` without a column list — FIXED
      2026-08-05**: positional bind in declared-column order; count
      mismatch raises SAP's 2129 byte-identically.
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
- [ ] **Views inside joins** — single-table view resolution landed
      2026-08-05 (derived-table route); a view used *within* a JOIN is
      still not resolved. Also open: aliased views (`FROM v x` with
      `x.col` qualifiers) and view-name row ordering in `system.views`
      (SAP shows creation order; OA map order).

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
