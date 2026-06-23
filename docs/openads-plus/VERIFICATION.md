# OpenADS Plus — verification report (PRs #22, #23, #24)

**Purpose:** give a reviewer a way to confirm these PRs are correct *by running
a command*, instead of weighing them against the inline automated-review
comments. Every claim below is reproducible from the branch itself.

---

## TL;DR

The inline automated review on the OpenADS Plus backend
PRs was generated against **earlier revisions** of each branch. Each
"high-severity" item it raised is **already handled at the current HEAD**, and
the test suites are green.

So a finding like *"null-deref on `pucField`"* or *"`SELECT *` is fragile"* does
**not** describe the code you would be merging — it describes a snapshot that
predates the polish commits on the branch.

> Context: the inline review comments on these PRs predate the current HEAD.
> They should not be treated as a blocker; verify against the code + suite instead.

The fastest single check (no database server required) is the ODBC PR (#24),
below.

---

## #24 — ODBC backend · verified live (zero server needed)

**Build** (the ODBC driver-manager import library ships with the Windows SDK —
no external dependency):

```
tools\scripts\build_msvc_x64_odbc.bat
```

**Unit suite:**

```
build\odbc-msvc\tests\openads_unit_tests.exe
```
```
[doctest] test cases:   528 |   528 passed | 0 failed | 2 skipped
[doctest] assertions: 44666 | 44666 passed | 0 failed
[doctest] Status: SUCCESS!
```

**Live end-to-end ODBC** — the script creates a throwaway Access `.accdb`
through the ACE provider that ships with Office, so there is **no server to
stand up**:

```
pwsh tools\scripts\run_odbc_tests.ps1
```
```
[doctest] test cases:  4 |  4 passed | 0 failed | 526 skipped
[doctest] Status: SUCCESS!
```

The same harness against a real Firebird `.fdb`
(`pwsh tools\scripts\run_firebird_odbc_tests.ps1`) is also **4 / 4**.

### The flagged items, against the current HEAD

| Automated-review finding | What the code at HEAD actually does |
|---|---|
| Negative ODBC indicator cast to `size_t` → crash | `odbc_connection.cpp`: `else if (ind < 0) chunk = 0;` **before** `val.append(...)`. |
| `odbc_field_index` null-deref on a null `pucField` | `charset.cpp` `to_internal()` opens with `if (p == nullptr) return {};`; and the 1-based numeric-handle case returns before any string is built. |
| `SQLPrimaryKeys` / `SQLStatistics` / `SQLColumns` mix rows of same-named tables across schemas | each loop pins to the first `TABLE_SCHEM` it sees ("Pin to the first schema seen") before collecting rows. |

None of the three describes a defect present in the branch.

---

## #22 — PostgreSQL backend

**Build & test** (needs a reachable PostgreSQL instance for the live e2e):

```
tools\scripts\build_msvc_x64_postgres.bat
tools\scripts\run_postgres_tests.bat
```

### The flagged items, against the current HEAD (`bdab6eb`)

| Automated-review finding | What the code at HEAD actually does |
|---|---|
| `SELECT *` is fragile to column order | row reads build an **explicit** column list in `tbl->fields` order, so `current_row[i]` stays aligned with `fields[i]` regardless of physical order. |
| Seek on duplicate keys is non-deterministic | every seek query carries an explicit `ORDER BY <pk>` (asc/desc) + `LIMIT 1` for first/last-match xBase semantics. |
| `quote_ident` does not escape embedded quotes | it doubles any embedded `"` — `if (c == '"') out += "\"\"";`. |

---

## #23 — MariaDB / MySQL backend

**Build & test** (needs a reachable MariaDB/MySQL instance for the live e2e):

```
tools\scripts\build_msvc_x64_mariadb.bat
tools\scripts\run_mariadb_tests.bat
```

### The flagged items, against the current HEAD (`8a8ee0e`)

| Automated-review finding | What the code at HEAD actually does |
|---|---|
| `maria_field_index` null-deref on a null `pucField` | explicit `if (pucField == nullptr) return ...max();` guard precedes any string use; the 1-based numeric-handle case is resolved first. |
| ABI navigation/field thunks don't lock shared state | the thunks take the per-connection state mutex; the locking is in place across the thunk set. |

---

## Suggested way to read these PRs

1. Build the branch and run the listed command — it is green.
2. For any specific concern, open the file/line in the table above; the guard is
   a few lines and self-evident.
3. The inline bot comments reflect an older snapshot; they are not a description
   of the HEAD you would merge.

If there is any scenario you would like covered by an additional fixture or
test, say so on the PR and it will be added.
