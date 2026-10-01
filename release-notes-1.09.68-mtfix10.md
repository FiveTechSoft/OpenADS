# v1.09.68-mtfix10 — WAN voucher save, round 10 (TEST BUILD)

**Built for Pritpal Bedi — bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.**

TEST ONLY — drop `ace32.dll` (Windows x86 zip) next to VouchADS.exe. Not for production.

## What changed (client-side only)

- **AdsGetNumLocks answers from the client lock ledger** while the ledger is
  provably complete (no append auto-lock outstanding, no table lock) — the
  same fast path mtfix8 gave AdsGetAllLocks. rddads polls lock counts after
  every commit/unlock, and these calls still rode the wire opcode even right
  after a real UnlockTable had provably emptied the ledger: ~9 RTTs per
  startup+save cycle in the mtfix9 trace.
- **FileExists probe for a table open on the connection answers locally.**
  A live table proves its own .dbf exists; the existing production-bag
  short-circuit now covers the table's own path too.
- **Existence/dir cache keys unify slash direction** (the server
  separator-normalizes, so the spellings are the same file). Case is
  deliberately NOT folded: the Linux server fs is case-sensitive.
- **Trace: every FileExists probe now logs its real path and outcome**
  (pos-cache / neg-cache / wire). The wire-op line's tid is a truncated
  hash that cannot tell same-length paths apart; this line can. The next
  trace will show exactly which files probe and why they miss.

## Expected effect

~0.5s off time-to-readiness (mtfix9 measured 37.9s). The lock-count polls
are the certain part; FileExists savings depend on how many probes hit the
new short-circuit — the new trace lines will quantify it.

## Locking discipline (unchanged)

No implicit locking anywhere: Seek stays pure, and only the app's explicit
dbRLock()/FLock() calls lock. mtfix10 changes only how lock *queries* are
answered, never when locks are taken.

## Validation

- New unit tests: GetNumLocks ledger fast path (incl. fallback to wire while
  the append auto-lock makes the ledger uncertain), open-table existence
  short-circuit.
- Full CI-tier suite green (1577/1577).

## Verification asks

1. Time-to-readiness number, same as before.
2. The new trace will contain `probe <path> -> yes/no (pos-cache|neg-cache|wire)`
   lines — send it over; those lines drive the next round.
