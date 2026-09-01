# Release notes — v1.09.19 (2026-09-01)

## Fixed — contended-lock wait slices: flat 100 ms retry → adaptive backoff (Pritpal Bedi)

"ADSCDX is imposing some wait slices on threads implementation which
has expanded the duration of operations." Confirmed and fixed.

**The problem.** When a record/table lock was contended, the client
retried with a **flat 100 ms sleep per attempt** (ACE defaults:
`retry_count` 10 × `cycle_ms` 100). Under low contention the retries
rarely fire and nothing is visible — but with 700 B_BIG instances × 10
threads contending on one table, every contended `RLock`/`FLock` paid
the full slices. Measured worst case for a single contended lock:
**1397 ms**, almost all pure sleep while the record was already free.

**The fix.** The retry loop now uses an adaptive ladder:

- attempts 1–6 recheck after 2/4/8/16/32/64 ms — the common case (the
  holder is inside a `REPLACE`, lock frees in a few ms) resolves in
  single-digit milliseconds instead of one 100 ms slice;
- later attempts keep the configured `cycle_ms` quantum;
- the loop continues until the **total budget** is exhausted
  (`retry_count × cycle_ms` ≈ 1 s — the plain SAP
  `AdsSetLockCycle`/`AdsSetLockRetryCount` contract), so
  `AE_LOCK_FAILED` timing semantics are unchanged.

`AdsSetLockCycle` / `AdsSetLockRetryCount` still control the ceiling.
Applied to the ABI `lock_with_retry` (local tables) and the wire client
`lock_record`/`lock_table` (the server already answers a contended lock
fail-fast).

**Measured:** contended-RLock worst case **1397 ms → 197 ms (7×)**, p50
unchanged. Full suite 1520/1520 (573,277 assertions), including the
multi-thread contention tests that pin the retry semantics.

## Upgrade notes

- Drop-in replacement for v1.09.x serverd and ace32/ace64.
- Combine with v1.09.18's append-regression fix: together, per-record
  cost under heavy multi-instance contention returns to the v1.8.x
  ballpark.
- Apps tuning the lock policy programmatically (`AdsSetLockCycle` /
  `AdsSetLockRetryCount`) keep full control of the ceiling.

## What to test (Pritpal)

1. Re-run B_BIG with 700 instances against this build
   (`max_sessions = 1000` in `openads.ini` as before).
2. Watch the wall-clock duration per instance — the contended
   RLock/FLock windows should no longer expand to ~1.4 s each.
3. Final record count must still be exactly 100 × instances.
4. If any operation still feels slow, note the stage (lock / append /
   browse) and the instance number from the window title.