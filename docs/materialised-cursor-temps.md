# Materialised cursor temps — why they are ADT, and what breaks if you change it

Some SELECT shapes cannot be served by a live cursor over the source table, so
the engine materialises the result into a **temp table** and hands back a cursor
over that instead. This note explains the constraints that pin the temp's
on-disk format, because two of them are invisible until a specific customer
scenario breaks, and both have already been regressed once.

**If you are about to change the format of a materialised temp, read this
first.** The relevant code is `exec_sql_direct_impl()` in
`src/abi/ace_exports.cpp` (search for `_srt_`).

## What materialises, and where

| Path | Temp | Site | Format |
|---|---|---|---|
| Single-table `SELECT` with `ORDER BY` / `DISTINCT` / `LIMIT` / `TOP` | `_srt_*` | `exec_sql_direct_impl()` | **ADT** ✅ |
| `EXECUTE PROCEDURE` declared outputs (`__output`) | `_spout_*` | `run_dd_procedure()` | **ADT** ✅ |
| Joins (2-table and N-way), unions, aggregates | various | same file | **DBF** ⚠️ still truncates |

A plain `SELECT ... [WHERE ...]` does **not** materialise — it stays a live
cursor. That asymmetry is why a bug here often looks like "the query works until
I add ORDER BY".

**The two fixed paths were fixed separately, months apart, for the same root
cause.** Fixing one and assuming the other was covered is exactly how the
`__output` truncation survived: `sp_GetPhysicalPath` was still returning
`databasepa` after the `_srt_` path was corrected. The join/union/aggregate
materialisers are still on DBF — same constraint matrix applies when you get to
them (joins additionally rename right-side columns `R_*`, itself a consequence
of squeezing `R_` + the source name into 10 bytes).

## The four constraints, and the format they force

| # | Requirement | Why | Rules out |
|---|---|---|---|
| 1 | Result must be a **standalone table**, not the live source | An application that runs `INDEX ON` / `DBSETORDER` over the result must not rewrite the *source's* production index. A real customer hit exactly this: "cualquier SELECT-SQL reescribio los indices originales" (#146). | a live cursor with a recno sequence |
| 2 | Column names must survive **in full** | A DBF field descriptor gives the name 11 bytes, so a DBF temp silently truncates every column to 10 chars. SAP's own catalog names are routinely longer — `RI_Primary_Table` (16), `Enable_Internet` (15), `User_Defined_Prop` (17), `Trig_Function_Name` (18) — so a DBF temp turned `SELECT Name, RI_Primary_Table FROM system.relations ORDER BY Name` into a result whose column was called `RI_Primary`, breaking every client that then referenced it by name. User tables with long columns were mangled identically. | a **DBF** temp |
| 3 | Numeric **scale** must survive | `N(12,2)` has to read back as `"10.50"`, not `"10.5"`. An ADT DOUBLE (type 10) stores no decimal count in its descriptor — and stamping one there makes the real ADS engine reject the table as corrupt (error 7016), so this is not something we can simply start writing. Reported by an ERP whose prices are `N(12,2)` (#146). | an ADT temp whose numerics map to **DOUBLE** |
| 4 | Column names must come from the **SELECT list**, not the descriptor | SAP names a result column the way the query wrote it, so `SELECT CLAIMKEY` over a column declared `claimkey` reports `CLAIMKEY`, and `AS ck` reports `ck`. A live cursor gets this from `cursor_aliases()`, but a temp carries its *own* descriptors, which that map cannot reach — so the names have to be baked into the temp's DDL as it is built. Forgetting this is invisible until a client compares result-column names, because the *values* are all correct. | reusing `field_descriptor(i).name` when building the DDL |

Constraints 2 and 3 are in direct tension, which is the trap: **DBF satisfies 3
but not 2, and the obvious ADT mapping satisfies 2 but not 3.**

Constraint 4 is easy to miss for the opposite reason — it is not a *format*
question at all, so fixing the naming rule at the ABI layer looks complete and
still leaves every `TOP` / `ORDER BY` / `DISTINCT` query wrong. Constraint 2 is
also its prerequisite: an alias longer than 10 characters needs the ADT temp.

The format that satisfies all three is an **ADT temp whose decimal numerics use
ADT type 2** (numeric stored as ASCII digits, which carries the declared scale
in the value itself). That is what the `AsciiNumeric` field-type name exists
for; see `field_spec_for()` / `adt_spec_for()` in `src/abi/ace_exports.cpp`.

## History — both halves have regressed before

- **#136** materialised these queries into a *memory table*. Full names, but it
  retyped every numeric as a 4-byte integer and capped records at 64 KB, so it
  dropped decimals and failed outright on a wide table.
- **#146** fixed the typing by switching to an on-disk temp — created as
  `ADS_CDX`, i.e. a DBF. That fixed constraint 3 and silently broke
  constraint 2 for every column name over 10 characters.
- The current code uses ADT + `AsciiNumeric`, satisfying all three.

## Copying rows into the temp

Values are copied with the setter that matches how the **target** field is
stored, not always the string one:

- `Numeric` / `Float` are ASCII in both DBF and ADT, so `as_string` carries the
  declared scale exactly and must be written as a string.
- `Integer`, `Double`, `Currency`, `ShortInt`, `AutoInc`, `AdtMoney` are raw
  little-endian binary on ADT. Writing a *string* into one stores the text
  verbatim, so `"10.50"` reads back as ~`6.01e-154`. This never showed on the
  old DBF temp because DBF stores numerics as ASCII, so the string write
  round-tripped by accident.

## Cleanup

`drop_materialised_cursor_temp()` deletes the temp when the cursor handle
closes. Its extension list must cover every format a materialiser can emit
(`.adt`/`.adm`/`.adi` today, plus the DBF-era extensions so temps left by an
older build are still removed). Anything missing leaks one file per query into
the customer's data directory.

## A note on `__output` specifically

Procedure outputs have no declared scale — `openads::script::Type` is
Char/Integer/Double/Logical/Date/Timestamp — so constraint 3 does not bite
there and no `AsciiNumeric` mapping is needed. Constraint 2 very much does: a
procedure may be reading ADT tables or declaring outputs that mirror SAP catalog
columns, and those names run past 10 characters routinely.

`run_dd_procedure()` builds the temp through `CREATE TABLE … AS FREE TABLE`,
which takes its format from the **statement's** table type and defaults to CDX
(i.e. DBF). It therefore pins `ADS_ADT` across that one DDL and **restores the
caller's setting afterwards** — the procedure body may run its own
`CREATE TABLE` and must not silently inherit our choice.

Because the temp is ADT, `sql_type_of()` no longer clamps `CHAR` to 254 (DBF's
character maximum). An over-long *record* is now rejected by `AdsCreateTable`
("ADT record too long") instead of being silently shortened.

## Numeric presentation — what a materialised cell must look like

A materialised path re-renders values instead of passing the stored bytes
through, so it has to reproduce the source's *presentation*, not just its
magnitude. Two rules, both oracle-probed against SAP on the mp corpus
(2026-07-30) and both encoded in helpers so the five materialisers cannot drift:

**1. Copied columns keep the source's width and scale, right-justified.**
`join_cell_text()` formats `%*.*f` into the declared width. It was `%-*.*f`
(left-justified) on the theory that leading spaces "leak" into string reads —
they are not a leak, they are the declared presentation, and SAP emits them. A
join must not render a column differently from a plain read of that column:

```
service.charges  N(11,2)   plain "      0.00"   join "      0.00"
```

**2. Aggregates have their own declared width** — `agg_result_width()`:

| aggregate | result width | note |
|---|---|---|
| `SUM` | source + 10 | a sum can carry far past the source's range |
| `AVG` / `MIN` / `MAX` | source width | cannot exceed the source |
| `COUNT` / `COUNT(*)` | integral, **not padded** | SAP returns `852033`, not `     852033` |

`agg_cell_text()` owns both the width and the justification, including the COUNT
exception, for exactly this reason.

> **Prerequisite: the ADT decimal count lives at byte 139, not 137.**
> `adt_driver.cpp` used to read 137 and got 0 for every SAP-written table. This
> hid for a long time because ADT type 2 stores numerics as ASCII text — a plain
> read returns the stored characters and never consults `decimals`. Only a path
> that re-formats from `as_double` needs the scale, so the first symptom was
> joins rendering money as `"0"` while the same column read correctly on its
> own. If a re-formatting path is dropping decimals, check the descriptor before
> suspecting the formatter.

## SAP's result-column naming rule

Pinned by probing `ace64.dll` directly against the mp corpus, not inferred.
Implemented in `build_projection_aliases()` in `src/abi/ace_exports.cpp`.

| Query | SAP reports |
|---|---|
| `SELECT CLAIMKEY FROM service` (declared `claimkey`) | `CLAIMKEY` |
| `SELECT s.CLAIMKEY, l.UNITS FROM …` | `CLAIMKEY`, `UNITS` — qualifier dropped |
| `SELECT CLAIMKEY AS ck` | `ck` |
| `SELECT s.ADM_NUM, l.ADM_NUM, s.adm_num` | `ADM_NUM`, `ADM_NUM_1`, `adm_num_2` |
| `SELECT * FROM admit` | the declared spellings, unchanged |

Three details that are easy to get wrong:

1. **The written spelling wins, not the declared one.** This is the opposite of
   the intuition that a column "has" a name.
2. **Repeats are suffixed `_1`, `_2`, … in select-list order**, and the *first*
   occurrence keeps the bare name.
3. **A suffixed name keeps its own item's case**, not the first occurrence's —
   hence `adm_num_2` above rather than `ADM_NUM_2`. So the collision is detected
   case-insensitively while the spelling stays per-item.

Because an alias need not exist on the underlying table, name-based reads
(`AdsGetField(hCur, "ck", …)`) must consult the cursor's names *before* the
table's, or they fail with 5063. `resolve_field_index_h()` does this.

## Regression tests

- `tests/unit/abi_sql_orderby_test.cpp` —
  *"materialised cursors keep column names longer than 10 chars"* covers
  constraints 2 and 3 together, across `ORDER BY`, `DISTINCT` and `TOP`.
- `tests/unit/abi_sql_orderby_types_test.cpp` — numeric scale (#146).
- `tests/unit/abi_script_proc_test.cpp` —
  *"script proc: `__output` keeps column names longer than 10 chars"* covers the
  procedure path, and also asserts the value is reachable **by** the full name,
  which is what a truncated descriptor actually breaks for callers.
- `tests/unit/abi_sql_projection_test.cpp` —
  *"S4 result columns are named from the SELECT list"* covers constraint 4. Its
  last subcase is the materialised one; the earlier subcases pass on the live
  cursor alone, so **a change that breaks only the temp still fails there** —
  that subcase is the guard, do not fold it into the others.

Each was verified to fail with its fix reverted — they guard, they do not merely
pass. If one fails with a truncated name or a re-rendered number, a materialising
path has gone back to a format that cannot carry both. Do not adjust the
expectations.

## Known related defect (not fixed here)

`AdsSetString` into an **ADT DOUBLE** field stores the text verbatim instead of
parsing it, so the value reads back as garbage. Reachable by creating an ADT
table with `Numeric,12,2` (which maps to DOUBLE) and writing with
`AdsSetString`. Tracked in `TODO.parity.md`.
