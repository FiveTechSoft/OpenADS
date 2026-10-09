# OpenADS 1.09.73-mtfix39: locked-row fresh-read test build

> Historical fork test-build notes. This document describes the original bedipritpal/OpenADS build, not validation or packaging of the upstream port. See docs/upstream-mtfix39-41-port.md for port scope and validation.

Prepared for Pritpal Bedi's bedipritpal/OpenADS fork, based on FiveTechSoft/OpenADS. Thanks to Pritpal for the 50-line invoice stock update log that pinned this regression. No private application source, traces or business data are included.

This test branch is fix/mtfix39-lock-row. It is not a merge to fork main and not an upstream submission. Pritpal's billing-counter checks are required before a main merge.

## What is in it

- mtfix39: a successful dbrLock after another station's write no longer serves the old row. The remote LockRecord ack now carries the freshly locked row (new negotiated wire capability kCapLockedRow, bit 0x00000400), so seek/lock/read/subtract/write/unlock stays at 4 wire frames per invoice line with no extra fetch. Old servers negotiate down: the client falls back to a real refresh after the lock instead.
- The client records the acquired lock in its ledger BEFORE any fallback refresh, so a failed refresh can never erase a real held lock.
- OPENADS_NO_LOCKED_ROW_CAP=1 makes the server simulate a pre-fix peer (no capability echo, no row trailer) for old-server negotiation testing.
- New regression coverage in tests/unit/abi_remote_locked_row_test.cpp: two-station stale-read repro, 50-line invoice replay at 4 frames/line, repeated lock/release, contended lock failure with no cache poisoning and no lock theft, old-server negotiation fallback, and lock freshness when the row sat stale in the read-ahead prefetch block.

## What to test

- Two billing counters on one stock table: counter A locks a line item right after counter B updates it; A must see B's committed qty, subtract, and write - the lost update is gone.
- Busy counter replay: many seek/lock/subtract/write/unlock cycles against openads_serverd from this build; wire frames per line stay at 4.

## Known findings held for later (not in this build)

- Speed candidates reviewed and deferred by design: single-record commit-and-release op (4->3 frames/line) and cross-line pipelining. Both need separate scoping; this is a correctness release.
- Pre-existing, unrelated to mtfix39: natural-order cross-session visibility lags. A plain skip-walk/goto read can serve rows cached before another station's commit, and a natural-order SetField+UnlockTable commit is not visible to peer sessions as promptly as the index-order write path. Lock and RefreshRecord reads are not affected. Repro available; recommend a separate fix branch.

## Packages

- openads-test-1.09.73-mtfix39-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
