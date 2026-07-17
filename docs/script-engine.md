# OpenADS Script Engine — Design

RCB 07/17/2026 — design agreed before implementation (workstream: replace the
ad-hoc script fragments with one real engine for the ADS SQL scripting
dialect).

## 1. Goal

OpenADS must execute **SQL-script stored procedures, user-defined functions,
and triggers** the way SAP Advantage Database Server does — because the goal
of the project is a drop-in replacement for the deprecated SAP ADS, not a
product that merely opens ADS data. Any `.add` whose procs/functions/triggers
run on SAP must run on OpenADS with the same results and the same errors.

Non-goals (for this workstream):
- The Advantage Extended Procedure (AEP) DLL ABI — native DLL procs already
  dispatch through `Connection::execute_procedure` and stay as-is.
- Query-engine features (joins, aggregates, dialect surface) — separate work.
- Performance beyond "parse once, execute many".

## 2. Current state (why this workstream exists)

There is no script engine today; there are three disjoint fragments and one
hole (file references as of commit `18be7d3d`):

| Caller | Today |
|---|---|
| UDF referenced in a SELECT | `namespace proc` in `src/abi/ace_exports.cpp` (~310 lines): string re-splitting per call, untyped (everything is `std::string`), operators limited to `+`/`-`, IF blocks **explicitly skipped**, a handful of builtins, nested calls only via a `SELECT f(args) FROM system.iota` recursion. |
| `EXECUTE PROCEDURE <dd-script-proc>` | **No execution path.** The dispatcher handles `sp_*` builtins and registered native DLL procs; a SQL-script proc stored in the DD returns "procedure not registered" (5000). |
| Trigger bodies | A third, separate handler with its own `__new`/`__old` subquery support; `EXECUTE PROCEDURE` inside a trigger is "not supported; skip" — trigger effects silently vanish and the DML reports success (SAP surfaces the error and fails the operation). |
| `EXECUTE IMMEDIATE` | Not implemented anywhere. |

Consequences measured against the SAP oracle (PMSYS corpus, 2026-07-16):
functions with control flow return empty; a function calling another function
returns wrong values (`EoM(2,2026)` → `02/00/2026` because the nested
`DaysInMonth` call yielded ""); every DD script proc errors; triggers "fire"
but their `EXECUTE PROCEDURE` effect is dropped silently.

The untyped model is itself a bug class: `""` silently coerced to day 0 in a
timestamp. A replacement engine must be **typed**.

## 3. The language — grammar inventory

Sources, in order of authority:
1. **The SAP oracle**: `f:\ads11\ace64.dll` (+ `asqlcmd64.exe`). Any grammar
   or semantics question is settled by running a probe script on SAP local
   server. This is the ground truth and every construct below gets an
   oracle-verified test before its implementation is called done.
2. **Real-world corpus**: the PMSYS bodies (7 procs, 9 functions, 6 triggers;
   dumps in `tools/qa-diff/out/sap_enf.json`) — marked [P] below where they
   exercise a construct.
3. ADS documentation knowledge (SQL PSM dialect) — marked [D]; anything not
   yet oracle-verified is listed in §9 Open Questions.

### 3.1 Statements

| Construct | Example | Evidence |
|---|---|---|
| `DECLARE <var> <type>;` | `DECLARE @cKey CHAR(13);` / `DECLARE nDays INTEGER;` | [P] both `@`-prefixed and plain identifiers occur |
| Assignment | `@s = @cprefix + Right(...);` / `nDays = DaysInMonth(m, y);` | [P] |
| `IF <cond> THEN <stmts> [ELSE <stmts>] ENDIF;` | [P] `newseqkey` | oracle-verify `ELSEIF` chain form |
| `WHILE <cond> DO <stmts> END WHILE;` | [D] | oracle-verify loop-exit keyword (`BREAK`/`LEAVE`?) |
| `WHILE FETCH <cursor> DO … END WHILE;` | [P] `sp_mgGetAllLocks…` | idiomatic ADS cursor loop |
| `RETURN [<expr>];` | [P] every function | |
| `DECLARE <cur> CURSOR AS <select-or-exec>;` | [P] `sp_GetPhysicalPath` | |
| `OPEN <cur>; FETCH <cur>; CLOSE <cur>;` | [P] | oracle-verify `FETCH <cur> INTO`… variant |
| Cursor field access | `@c.fieldname` in expressions | [D] | 
| Embedded DML/SELECT | `INSERT INTO __output …`, `MERGE INTO sequences …`, `UPDATE/DELETE/SELECT` | [P] `newseqkey` uses MERGE |
| `EXECUTE PROCEDURE name(args);` | [P] trigger bodies call `sp_SaveIntoAuditLog` | |
| `EXECUTE IMMEDIATE <string-expr>;` | [P] `sp_GetPhysicalPath` | |
| `TRY … CATCH [ALL] … [FINALLY …] END TRY;` | [D] | oracle-verify exact grammar + error variable |
| `RAISE <name>(<code>, <msg>);` | [D] | oracle-verify |
| Transactions in scripts | `BEGIN TRANSACTION; COMMIT; ROLLBACK;` | [D] oracle-verify interplay with caller txn |
| Comments | `-- line`, `// line`, `/* block */` | [P] both `--` and `//` occur in PMSYS |

### 3.2 Expressions

- Operators: `+ - * /`, string concat `+`, comparisons `= <> != < <= > >=`,
  `AND OR NOT`, `LIKE`, `IN (…)`, `BETWEEN`, `IS [NOT] NULL`. (The current
  fragment supports only `+`/`-` — full set required.)
- `CASE <x> WHEN v THEN r … [ELSE d] END` and searched
  `CASE WHEN cond THEN r … END` — [P] `newseqkey`.
- `IIF(cond, a, b)` — [P].
- Subquery as value: `@x = (SELECT … );` and subquery inside larger
  expressions — [P] `newseqkey`, `eom` pattern. Delegated to the SQL engine.
- Scalar function calls: the ADS scalar library (`SUBSTRING`, `POSITION`,
  `TRIM`, `CONVERT`, `CREATETIMESTAMP`, `CURDATE`, `YEAR`… ) — resolved
  through the same scalar-function registry the SELECT evaluator uses, so
  scripts and queries can never disagree.
- UDF calls (script functions calling script functions) — [P] `EoM` →
  `DaysInMonth`. Direct engine recursion, not SQL round-trip.

### 3.3 Types and values

Script values carry the ADS SQL type system: `CHAR/VARCHAR/CICHAR`, `MEMO`,
`SHORT/INTEGER/AUTOINC`, `NUMERIC/DOUBLE/MONEY/CURDOUBLE`, `DATE`,
`TIMESTAMP`, `TIME`, `LOGICAL`, `NULL`. Semantics that must match SAP (each
oracle-verified):
- NULL propagation in arithmetic/concat/comparison.
- DATE ± integer, TIMESTAMP components, date→string casts honoring the
  connection date format.
- Numeric/string coercions (`'5' + 1`, `STR()`, `VAL()` behavior).
- LOGICAL literals `TRUE/FALSE` [P].

### 3.4 System objects visible to scripts

- `__input` / `__output` — proc parameter/result virtual tables [P].
- `__old` / `__new` — trigger row images [P].
- `system.iota` — 1-row table for scalar SELECTs [P].
- `LASTAUTOINC( STATEMENT|CONNECTION )`, `::stmt`-style variables — [D],
  oracle-verify which are used in the wild before implementing.

## 4. Architecture

New module: `src/engine/script/` (no dependency on the ABI layer):

```
src/engine/script/
  lexer.h/.cpp        tokens: idents (@-aware), literals, operators, comments
  ast.h               Stmt/Expr node types (std::variant-based)
  parser.h/.cpp       text -> Program {vector<Stmt>}; ADS-style errors
  value.h/.cpp        ScriptValue: tagged union + ADS coercion rules
  exec.h/.cpp         Executor: scopes, control flow, cursors, TRY/CATCH
  bridge.h            SqlBridge + CallContext interfaces (see below)
```

### 4.1 Compilation model

`script::compile(text) -> Result<Program>`; parsed **once**, cached on the
DataDict entry (`shared_ptr<const Program>`, keyed by body hash, invalidated
by ALTER/drop). Parse errors surface with ADS-compatible codes (7200 family)
at first execution, like SAP.

### 4.2 SQL delegation — the key decision

The script engine executes **only** the scripting constructs. Every embedded
SQL statement (SELECT/INSERT/UPDATE/DELETE/MERGE/DDL/EXECUTE PROCEDURE) and
every subquery is delegated through an interface the ABI layer implements:

```cpp
struct SqlBridge {
    // Execute one SQL statement on the caller's connection/transaction,
    // with script variables bound as named parameters.
    virtual util::Result<Cursor> exec(const std::string& sql,
                                      const std::vector<Bound>& vars) = 0;
    virtual util::Result<ScriptValue> call_udf(const std::string& name,
                                      std::vector<ScriptValue> args) = 0;
};
```

Variable references inside embedded statements (`WHERE field = cField`,
`VALUES (@x)`) bind through the existing named-parameter machinery; where a
statement form doesn't support parameters yet, the bridge substitutes a
correctly-quoted literal (documented fallback, removed as parameter support
widens). Resolution order (variable vs column name) is oracle-verified and
encoded in one place.

This keeps the script engine small and guarantees scripts and ad-hoc SQL
can't diverge: there is exactly one SQL executor.

### 4.3 Execution contexts

One `Executor`, three entry shapes provided by the caller via `CallContext`:
- **Function**: parameter values in scope; `RETURN expr` yields the value.
- **Procedure**: `__input` populated from `EXECUTE PROCEDURE` args;
  `__output` is a memory table (existing `build_memory_result` machinery)
  returned as the statement cursor — exactly SAP's shape.
- **Trigger**: `__old`/`__new` row accessors; no `__output`.

### 4.4 Error model

Every statement evaluates to `Flow { Normal | Return(v) | Exception(Error) }`
(+ loop-exit once the keyword is oracle-confirmed). TRY/CATCH consumes
`Exception`; an uncaught `Exception` aborts the script and propagates a real
ADS error code to the caller. **Trigger call sites fail the DML operation on
script error** — this fixes the silent-swallow defect by construction (SAP
parity; exact error code oracle-verified).

### 4.5 Call-site integration (all four converge)

1. UDF-in-SELECT (`K::Udf` in the SELECT evaluator) → `Executor` (function
   context). The old `proc::` namespace is deleted.
2. `EXECUTE PROCEDURE` dispatcher: after `sp_*` builtins and registered
   native procs, a DD script proc compiles + runs (procedure context),
   cursor = `__output`.
3. Trigger firing path → `Executor` (trigger context) with error
   propagation.
4. `EXECUTE IMMEDIATE` → evaluate the string expression, hand it to
   `SqlBridge::exec` (i.e. recursion into the one SQL entry point).

## 5. Phasing

Each phase lands green (full suite + clang CI-parity build) and is verified
against the SAP oracle before the next starts.

**S1 — language core.**
Lexer, parser, AST, typed `ScriptValue`, scopes; assignments; full expression
evaluator (all operators, CASE, IIF, NULL rules); IF/ELSE/ENDIF; WHILE;
RETURN; direct UDF-to-UDF calls. Switch the UDF call site over; delete
`proc::`. Embedded SQL in S1 may still route through the bridge in
literal-substitution mode.
*Gate:* per-construct unit tests + oracle differential battery for
expressions/control flow; PMSYS functions `BoY/EoY/EoM/DaysInMonth/
MonthsRented/MonthsOnTheMarket/NewSeqKey` all match SAP.

**S2 — SQL integration.**
Named-parameter binding for embedded statements; subquery-into-variable;
`EXECUTE PROCEDURE` for DD script procs with `__input`/`__output`; trigger
bodies switched over with error propagation (DML fails when the trigger
fails).
*Gate:* oracle differential for procs; PMSYS `sp_SaveIntoAuditLog` runs and
the trigger→proc chain writes audit rows identically to SAP; a failing
trigger aborts the UPDATE on both engines.

**S3 — completions.**
Cursors (`DECLARE … CURSOR AS`, OPEN/FETCH/CLOSE, `WHILE FETCH`, `@c.field`);
`EXECUTE IMMEDIATE`; TRY/CATCH/FINALLY + RAISE; transaction statements;
scalar-builtin coverage sweep driven by the differential battery.
*Gate:* all 7 PMSYS procs return SAP-identical results; differential battery
green across the full construct matrix.

## 6. Test plan

- **Engine-level unit tests** (no ABI): `tests/unit/script_lexer_test.cpp`,
  `script_parser_test.cpp`, `script_exec_test.cpp` — one TEST_CASE per
  construct, including error cases (parse errors, NULL edge cases, type
  coercions).
- **ABI-level tests**: `abi_script_udf_test.cpp`, `abi_script_proc_test.cpp`,
  `abi_script_trigger_test.cpp` — DD-created objects executed through
  SQL/`EXECUTE PROCEDURE`, including the trigger-failure-aborts-DML case.
- **Oracle differential harness**: extend `tools/qa-diff` with a script
  battery (`tools/qa-diff/scripts/*.sql`, each a self-contained script +
  expected shape) run against `ace64.dll` and `openace64.dll`; results and
  error codes diffed. This is also how every §9 open question gets settled —
  the probe becomes a battery entry.
- **PMSYS**: remains an integration fixture (the existing sweep), not the
  target.

## 7. What this replaces / deletes

- `namespace proc` in `ace_exports.cpp` (entire string interpreter).
- The trigger-body statement fragment (its `__new`/`__old` handling moves
  into the Executor's trigger context).
- The "not supported; skip" branches for EXECUTE PROCEDURE / EXECUTE
  IMMEDIATE inside triggers.

## 8. Risks

- **Grammar unknowns** — mitigated by the oracle-probe-first rule (§3, §9):
  no construct is implemented from memory alone.
- **Named-parameter coverage** in the SQL executor may be narrower than
  scripts need → literal-substitution fallback in the bridge keeps S1/S2
  moving; each fallback logged so it shrinks over time.
- **Size** — largest subsystem since prefetch; phased gates keep main green
  and each phase independently shippable.
- **Recursion** (UDF→UDF, proc→proc, trigger→proc→trigger) — depth limit
  with an ADS-style error (oracle-verify SAP's limit and code).

## 9. Open questions (each becomes an oracle probe before its phase)

1. Loop-exit keyword inside WHILE (`BREAK`? `LEAVE`?) and `CONTINUE`?
2. Exact TRY/CATCH grammar: `CATCH ALL`? error object/variable exposed?
3. `RAISE` syntax and how user errors surface to the client (code, class).
4. Variable-vs-column resolution order inside embedded statements.
5. `FETCH <cur>` forms (`FETCH NEXT`? `INTO`?) and cursor updatability
   (`WHERE CURRENT OF`?).
6. Trigger failure semantics: exact error code, and whether the failed DML
   rolls back statement-wide or record-wise on SAP.
7. Transaction statements inside procs when the caller already has an open
   transaction.
8. Recursion depth limit and its error code.
9. `DECLARE` without `@`: scoping rules when a parameter and a DECLAREd
   variable collide, case sensitivity of variable names.
10. Which `::connection`/`LASTAUTOINC`-class system values scripts can read.
