# B_BIG storm 700 — engine races (design)

Date: 2026-09-02
Status: approved (brainstorming), pending implementation
Author: grok
Evidence: `C:\Users\Anto\Downloads\b_big.zip` against `openads_serverd` 1.09.16
          with Pritpal's `openads.ini` (`:6262`, `legacy_paths`, `enableFileFunc`,
          `max_sessions=1000`). One instance: 100/100 clean. Storm 700: stall.

## Problem

Pritpal's `B_BIG.exe` (Harbour x86, `ace32.dll`, GT WVT) launches N OS
processes against one remote table `C:/Temp/TestFolder/TestIndex.dbf`.
Each process: mutex instance id, `AdsConnect60`, 9 worker threads + main
all call `HeyStageDbf` (create folder/table/3-tag CDX if missing, append
10 named rows, `DbCommit`/`dbUnlock`). Main then `Browse()`.

Acceptance is the **unmodified** exe:

- N=700 → **70 000** records (10 threads × 10 appends × 700)
- geometry exact (`hlen=258`, `rlen=71`, size = hdr + nrec·rlen + 1)
- 0 blank `NAME` / `INS` / `RDD`
- 700 distinct `INS` values 1..700, 100 recs each
- CDX tags IDX01/IDX02/IDX03 present
- 0 process crashes

Observed on 2026-09-02 (spawn 700/700 in 11.1s, 0 crashes):

| Signal | Count | Meaning |
|---|---|---|
| nrec stalled at 22084 from t=462s | — | appends stopped; 700 processes still in Browse |
| geometry hdr=walk | 22084 | file is well-formed, not torn |
| instances with exactly 100 recs | 214 | rest never finished HeyAddRecords |
| blank INS / NAME / RDD | 580 / 244 / 606 | APPEND persisted, REPLACE sequence cut |
| ads_err 5103 OpenIndex | 4 | `TestIndex.cdx` |
| ads_err 7040 CreateTable | 25 | SAP-correct (`AE_FILE_IN_USE`) |
| ads_err 5000 SetOrder | 30 | default `err()` code, not a real 5000 |
| ads_err 5012 Lock | 1 | `AE_LOCKED` |

The in-process C++ storm `tests/unit/abi_remote_create_stress_test.cpp`
already passed N=700. It is **not** B_BIG: it has a barrier after INDEX ON,
no `dbSetOrder(0)`, no `Browse()`, one thread per connection. That is why
it is green and B_BIG is not.

## Non-goals

- Do not change `b_big.prg` / `B_BIG.exe`. Pritpal's binary is the gate.
- Do not treat 7040 CreateTable as a bug (SAP: create over an OPEN table).
- Do not add append-lock queues / backoff as the first patch. Revisit only
  if B still stalls after the three correctness fixes.
- Do not retune `max_sessions` / `backlog`. 700 sessions < 1000.

## Root causes

### 1. OpenIndex vs INDEX ON → 5103

`AdsCreateIndex61` serialises bag create with `create_path_mu_for(path)`
(`ace_exports.cpp`). `AdsOpenIndex` / `CdxIndex::list_tags` do **not**.

`list_tags` opens the bag `ReadOnly` and, if the header is short, returns
6106. `File::open` on a missing bag maps `ERROR_FILE_NOT_FOUND` to **5103**
(`file_win32.cpp` `os_error`). Production auto-open and `dbSetIndex` during
the create window therefore log `OpenIndex: TestIndex.cdx` / 5103.

The DBF create path already closed the analogous 5103 "header truncated"
storm. The CDX open path did not get the same mutex.

### 2. SetOrder → 5000 (`bad index id`)

Server `Opcode::SetOrder` (`session.cpp`):

```
if (iit == index_h_.end()) { reply = err("SetOrder: bad index id"); break; }
```

`err(msg)` defaults to `AE_INTERNAL_ERROR` (5000). After a failed OpenIndex
the session has no index id, so `dbSetOrder(0)` / `AdsSetIndexOrderByHandle`
nacks 5000 and poisons the workarea.

ABI local path already treats `hIndex == 0` as natural order
(`AdsSetIndexOrderByHandle`). The **wire** path does not: iid 0 / missing
is an error instead of `AdsSetIndexOrder(ht, nullptr)`.

Client `RemoteConnection::set_order` also flattens any non-Ack opcode to
5000, hiding 7040/5012/5103.

### 3. Blank records: append lock is best-effort

`Table::append_record` (`table.cpp`):

```
(void)try_lock_record_excl(recno_);
```

ACE / xBase: a shared-mode append **must** hold an exclusive record lock
so the following field puts pass GoHot. `try_lock` is non-blocking
(`ByteLock::try_acquire`). CDX compatible lock bytes **alias** across
recnos, so under 7000 writers the try fails. The blank row is already on
disk (`append_record_raw` bumped the header). Later `REPLACE`s die or
stop mid-field (`NAME`+`CITY` filled, `INS`/`RDD` empty). Harbour only
retries `DbAppend` once.

`LockMgr::lock_record_excl` (blocking `ByteLock::acquire`) already exists.
Append must use that, and must **fail the append** (not persist an unlocked
blank) if the lock cannot be taken.

## Design

Three server/ACE patches. Same `B_BIG.exe` is the acceptance test.

### Patch 1 — OpenIndex waits for bag create

- `AdsOpenIndex` (local) and the session `Opcode::OpenIndex` handler take
  `create_path_mu_for(resolved_bag_path)` for the duration of
  `list_tags` / tag open, the same mutex `AdsCreateIndex61` already holds
  around the create-or-attach decision.
- After the lock, missing bag → `AE_NO_MATCHING_FILE` / existing
  `AdsOpenIndex` not-found path (not 5103-as-corrupt). Truncated header
  after the creator has released the mutex is still 6106 (real corrupt).
- `file_win32.cpp` / `file_posix.cpp`: map sharing-violation / `EAGAIN` /
  `ETXTBSY` on index/table open to **7040** (`AE_FILE_IN_USE`), not 5000
  or 5103. Keep 5103 for true not-found.
- No change to 7040 CreateTable semantics.

### Patch 2 — SetOrder natural / recover

- `Opcode::SetOrder`: `iid == 0` or missing map entry → treat as natural
  order (`AdsSetIndexOrder(ht, nullptr)` / drop `ordered_tables_`), **Ack**.
  Do not `err()` 5000.
- If iid is non-zero and missing: one retry — `AdsOpenIndex` on the
  table's production bag, register new `index_h_` ids, then
  `AdsSetIndexOrderByHandle`. Still missing → `err(..., AE_NOT_FOUND)`
  (or the Ads* code), never default 5000.
- `RemoteConnection::set_order` / `set_order_by_name` / `open_index`:
  when the reply opcode is `Error`, return the u32 ACE code from the
  payload. Stop inventing 5000 on nack.
- Local `AdsSetIndexOrderByHandle(h, 0)` stays as-is (already natural).

### Patch 3 — Append lock is mandatory

- `Table::append_record`: replace `(void)try_lock_record_excl(recno_)`
  with `lock_record_excl(recno_)`. On lock failure, do not leave a
  committed blank: undo the raw append (rewrite header count + truncate
  the extra record + 0x1A) **or** fail before `append_record_raw` if a
  lock on `rec_count_+1` can be reserved first. Prefer lock-then-write
  if the lock byte is a function of recno and recno is known
  (`rec_count_+1` under the header lock).
- Session `AppendBlank` already goes through `AdsAppendRecord`; it
  inherits the fix. If Ads* returns 5012, the client sees append failure
  and B_BIG's one retry can succeed.
- Field put on a pending append without the rec lock: 5035/5012, no
  partial write. Existing GoHot guard should already do this; add a test
  that a failed lock never produces a durable blank row.

### Tests (TDD, in-tree)

Extend, do not replace, the existing storms:

| Test | What it pins |
|---|---|
| New: OpenIndex concurrent with AdsCreateIndex61 on a fresh bag (no barrier) | 0× 5103; every opener gets 0 or 7040 then retry-success; bag well-formed |
| New: remote `AdsSetIndexOrderByHandle(h, 0)` with empty `index_h_` | Ack, natural order, subsequent append works |
| New: N connections Append+7×SetField+Write+Unlock without index barrier | 0 blank NAME/INS; nrec == N; every rec locked between append and unlock |
| Existing `abi_append_autolock_test.cpp` | still green (auto-lock visible) |
| Existing `abi_remote_create_stress_test.cpp` | still green; add a **no-barrier** sibling or flag so INDEX ON and OpenIndex overlap |
| Manual: unmodified `B_BIG.exe` × 700 vs serverd + Pritpal INI | 70 000 / 0 blanks / 700 INS |

Worker count for the C++ no-barrier sibling: start at 32, then 120. The
700-process Harbour storm is the product gate, not the unit default
(WVT × 700 is a desktop soak).

## Files (expected)

- `src/abi/ace_exports.cpp` — OpenIndex mutex; append lock-then-write
- `src/engine/table.cpp` — `append_record` blocking lock + rollback
- `src/network/session.cpp` — SetOrder iid 0/missing; OpenIndex already calls AdsOpenIndex
- `src/network/client.cpp` — preserve ACE codes from Error frames
- `src/platform/file_win32.cpp`, `file_posix.cpp` — sharing → 7040
- `src/drivers/cdx/cdx_index.cpp` — list_tags under create mutex **or** rely on AdsOpenIndex holding it (prefer the ABI so every caller is covered)
- `tests/unit/` — new cases above + stress sibling
- `CHANGELOG.md` — one entry, credit Pritpal Bedi

## Success

`B_BIG.exe` from `b_big.zip`, `ace32.dll` = current `openace32.dll`,
serverd with Pritpal's INI, `run_exe.bat B_BIG.exe 700` (or equivalent
minimized spawn):

Two independent counts agree: DBF header nrec **and** sequential walk
== 70000; 0 invalid delete flags; 700 INS; CDX three tags walk 70000.
`ads_err.dbf` may still contain 7040 CreateTable (benign). It must not
contain 5103 OpenIndex or 5000 SetOrder from this run.
