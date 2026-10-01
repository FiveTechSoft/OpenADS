# OpenADS test build 1.09.68-mtfix8 (TEST ONLY - not for production)

Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on mtfix7-diag / 1.09.68. Not an official release. Nothing merged to main or upstream.

## What's in it
Everything in 1.09.68-mtfix7-diag, plus four client-side round-trip cuts found by your wire trace (no Vouch application change):

1. Commit slimming. The trace showed each voucher save paying frames that release or flush nothing:
   - UnlockTable is skipped when the client knows no locks are held (rddads calls it before every lock and every append - 1 round trip each time).
   - GetAllLocks is answered from the client's own lock list when that list is provably complete (1 round trip after every commit).
   - COMMIT on a table with no changes since the last flush sends nothing (kills the 16-frame flush storm after every voucher save).
   - With an mtfix8 SERVER, FlushTable now includes the full file flush, so the trailing FlushFileBuffers frame goes away too (one frame per commit instead of two). This one saving needs the server updated; the other three work against your current server. Mixed old/new is safe both ways - the old pair of frames is kept automatically when the server is older.
2. Locked-then-released tables park instead of closing. GN_COUNT, FA_ACC01 and USASELOG each paid a full 7-frame reopen per save because they had once held a lock. Locks actually still held at close still force a real close, so no lock can leak to other users.
3. Directory and missing-file answers cache per session. Startup DirExist/DirMake probes and "is this optional file there" checks stop repeating round trips. Note: a file created by another workstation mid-session can be reported missing until your session itself creates or deletes something - tell me if you hit that.

## What to test
- Turn on wire_trace as before, post one voucher, send the new log. The save should show "skipped: no locks held", "clean: flush skipped" and "served from client lock ledger" lines, far fewer FlushTable/FlushFileBuffers frames, and no full reopen of GN_COUNT.
- Expected: roughly 40-60 fewer round trips per voucher save than the mtfix7-diag trace (about 2-3s at your ~43ms latency), plus up to ~1.5s off startup.
- Also run a normal startup, a reindex, and two or three Vouch instances at once - the lock handling changed, so watch for any user being blocked from a record they just unlocked (that would be a bug, report it).
- For the full saving, update openads_serverd on Lightsail with this build using your usual script. Client-only already gives most of it.

## Packages
- openads-1.09.68-mtfix8-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
