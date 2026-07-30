# Materialised cursor temps — why they are ADT, and what breaks if you change it

Some SELECT shapes cannot be served by a live cursor over the source table, so
the engine materialises the result into a **temp table** and hands back a cursor
over that instead. This note explains the constraints that pin the temp's
on-disk format, because two of them are invisible until a specific customer
scenario breaks, and both have already been regressed once.

**If you are about to change the format of a materialised temp, read this
first.** The relevant code is `exec_sql_direct_impl()` in
`src/abi/ace_exports.cpp` (search for `_srt_`).

## Which queries materialise

Single-table `SELECT` with any of `ORDER BY`, `DISTINCT`, `LIMIT`/`TOP`.
A plain `SELECT ... [WHERE ...]` does **not** — it stays a live cursor. That
asymmetry is why a bug here often looks like "the query works until I add
ORDER BY".

Joins, unions, aggregates and `EXECUTE PROCEDURE` result sets have their own
materialisers elsewhere in the same file; they are **not** covered by this note
and still carry the DBF constraints described below.

## The three constraints, and the format they force

| # | Requirement | Why | Rules out |
|---|---|---|---|
| 1 | Result must be a **standalone table**, not the live source | An application that runs `INDEX ON` / `DBSETORDER` over the result must not rewrite the *source's* production index. A real customer hit exactly this: "cualquier SELECT-SQL reescribio los indices originales" (#146). | a live cursor with a recno sequence |
| 2 | Column names must survive **in full** | A DBF field descriptor gives the name 11 bytes, so a DBF temp silently truncates every column to 10 chars. SAP's own catalog names are routinely longer — `RI_Primary_Table` (16), `Enable_Internet` (15), `User_Defined_Prop` (17), `Trig_Function_Name` (18) — so a DBF temp turned `SELECT Name, RI_Primary_Table FROM system.relations ORDER BY Name` into a result whose column was called `RI_Primary`, breaking every client that then referenced it by name. User tables with long columns were mangled identically. | a **DBF** temp |
| 3 | Numeric **scale** must survive | `N(12,2)` has to read back as `"10.50"`, not `"10.5"`. An ADT DOUBLE (type 10) stores no decimal count in its descriptor — and stamping one there makes the real ADS engine reject the table as corrupt (error 7016), so this is not something we can simply start writing. Reported by an ERP whose prices are `N(12,2)` (#146). | an ADT temp whose numerics map to **DOUBLE** |

Constraints 2 and 3 are in direct tension, which is the trap: **DBF satisfies 3
but not 2, and the obvious ADT mapping satisfies 2 but not 3.**

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

## Regression tests

- `tests/unit/abi_sql_orderby_test.cpp` —
  *"materialised cursors keep column names longer than 10 chars"* covers
  constraints 2 and 3 together, across `ORDER BY`, `DISTINCT` and `TOP`.
- `tests/unit/abi_sql_orderby_types_test.cpp` — numeric scale (#146).

If either fails with a truncated name or a re-rendered number, a materialising
path has gone back to a format that cannot carry both. Do not adjust the
expectations.

## Known related defect (not fixed here)

`AdsSetString` into an **ADT DOUBLE** field stores the text verbatim instead of
parsing it, so the value reads back as garbage. Reachable by creating an ADT
table with `Numeric,12,2` (which maps to DOUBLE) and writing with
`AdsSetString`. Tracked in `TODO.parity.md`.
