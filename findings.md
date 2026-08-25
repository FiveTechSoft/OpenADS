# Findings — Remote SEEK returns blank/garbage CHARACTER fields (Vouch login bug)

**Date:** 2025-08-25 · **Repo:** C:\OpenADS · **Status:** FIXED (uncommitted)

## 1. Symptom (Pritpal Bedi, Vouch)

Running Vouch against OpenADS (ace64.dll, remote server):

- `SEEK` **succeeds** (`Found()=.T.`, `RecNo()` correct) but `FieldGet()` returns
  blank/garbage values.
- **Only CHARACTER fields** go blank; numeric / date / logical / memo read correctly.
- **Not consistent across tables**: some tables read fine, some fail.
- Plain DBF/CDX (local RDD) with the same code works.
- Coding style: current-alias only (`SELECT("USERS")` + plain `SEEK`/`FieldGet`),
  no `(alias)->()` scoping. HbDBU shows the garbage bytes physically on disk for
 records the app appended after a failed lookup.

## 2. Root cause — stale per-workarea EOF flag in the remote client

Chain (all inside the client `ace64.dll`, no wire change):

1. A previous navigation of that workarea landed on the EOF phantom
   (e.g. a browse walk, `SKIP` past the last record). The remote client's
   `RemoteTable::nav_at_eof` flag was set to `true`.
2. `AdsSeek` / `AdsSeekLast` (remote branch, `src/abi/ace_exports.cpp`) reset
   `row_valid` and the prefetch queue, but **did not reset `nav_at_eof` /
   `nav_at_bof`**. After a *successful* seek onto a live record, `AdsAtEOF()`
   still reported `.T.` (verified with client trace:
   `AdsAtEOF: nav_eof=1 row_valid=1` while sitting on recno 1 of 3).
3. rddads `adsSeek` calls `hb_adsUpdateAreaFlags` (`C:\harbour\contrib\rddads\ads1.c:418`):
   `fPositioned = !BOF .AND. !EOF` → becomes `.F.`
4. rddads `adsGetValue` (`ads1.c:2200`): **only the `HB_FT_STRING` case checks
   `fPositioned`** and returns blanks *without calling ADS at all* when it is
   false. Numeric/logical/date/memo cases call ADS directly and get correct
   values. → exactly "only character fields are blank".
5. If the app then appends (login pattern: not found → add), the blank values
   are written to disk — which is what HbDBU showed.

### Why "some tables, not others"

The poison is **per workarea**, not per table and not alias confusion: only an
area whose navigation previously hit EOF fails. Verified with repro4 (two
aliases, USERS + ELE, old DLL):

| Seek (current alias, Vouch style)     | Found | Char fields |
|---------------------------------------|-------|-------------|
| ELE right after its own walk to EOF   | .T.   | **blank**   |
| ELE seek 'fy' (same poisoned area)    | .T.   | **blank**   |
| USERS (area never landed on EOF)      | .T.   | correct     |

## 3. Fix

`src/abi/ace_exports.cpp`, remote branches of `AdsSeek` and `AdsSeekLast`:
after the seek ack, recompute the nav flags from the seek outcome
(comment `FIX(seek-nav)`):

- hit, or `recno != 0` → `nav_at_bof = nav_at_eof = false`
- miss with `recno == 0` → genuinely at EOF → `nav_at_eof = true`

No extra round-trip, no wire protocol change, no rddads change, no change to
alias/workarea semantics (handles and nav state are already per workarea).

## 4. Verification

Repro suite (Harbour + rddads against ace64.dll), `C:\Users\Anto\AppData\Local\Temp\opencode\ads_repro\`:

| Repro   | Scenario                                              | Old DLL | Fixed DLL |
|---------|-------------------------------------------------------|---------|-----------|
| repro1  | C/N fields, seek+append, local + remote               | remote seek: char blank | pass |
| repro2  | full type matrix C/N/F/L/D/M + high bytes             | (local only) | pass |
| repro3  | remote, seek after EOF walk                           | **char blank** | pass |
| repro4  | multi-area, current-alias only, Vouch style           | **poisoned area blank** | pass |
| worker5 | separate processes/threads, own connections, EOF walk + seek | — | pass |

Repo test suite: `tests\ttest.cmd "*seek*" "*Nav*" "*Skip*" "*row*" "*trailer*" "*fetch*" ...`
→ **217 cases, 0 failures** with the fix.

Pre-existing failures (fail identically WITHOUT the fix, confirmed via `git stash`):
- `Remote server and local client alternate duplicate-key appends`
- `MT: 8 writer threads x 50 duplicate-key appends (remote server)`
- `remote append invalidates keyno; Refresh pattern stays O(1)`

## 5. Build notes (this machine)

- Alaska Xbase++ sets `INCLUDE`/`LIB` machine-wide to `xpp20`; `cl.exe` spawned
  by hbmk2 then fails. Use a short `PATH` (harbour + MSVC bin only) instead of
  full `vcvars64.bat`.
- Harbour smoke builds: `hbmk2 -comp=msvc64 -gtcgi -iC:\harbour\contrib\rddads
  -lrddads -lhbct -lace64 <prg>`; copy `openace64.dll` as `ace64.dll` next to the exe.
- No `hbthread.lib` in this Harbour build → thread demos done with concurrent
  processes (equivalent isolation at the ACE/connection level).

## 6. Deliverables

- `src/abi/ace_exports.cpp` — the fix (uncommitted).
- Rebuilt: `openace64.dll` (→ `ace64.dll`), `openads_serverd.exe`.
- For Pritpal: replace `ace64.dll` in the Vouch ADS installation and retest the
  login; no server update required (but the rebuilt serverd is compatible).
