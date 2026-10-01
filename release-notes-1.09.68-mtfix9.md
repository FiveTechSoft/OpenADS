# OpenADS test build 1.09.68-mtfix9 (TEST ONLY - not for production)

Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on mtfix8 / 1.09.68. Not an official release. Nothing merged to main or upstream.

## What's in it
Everything in 1.09.68-mtfix8, plus one client-side round-trip cut:

Navigation stamps survive the CloseAllIndexes/OpenIndex rotation. rddads closes and reopens the index set around many operations, and each reopen paid an extra GotoTop round trip to reposition (57 of those frames in your mtfix8 trace). The client now parks the position stamp when the index set closes and restores it on the matching reopen when the order is unchanged, so that frame goes away. Client-only change, no wire-protocol difference - works against any server version, mixed old/new is safe.

## Measured so far (your mtfix8 trace)
- Round trips: 1,079 -> 1,009, wire time 47.1s -> 44.1s.
- Voucher save: 280 -> 230 round trips, 12.4s -> 9.7s wire.
- Flush storm gone: FlushTable 46 -> 15, FlushFileBuffers 19 -> 0.

## What to test
- Turn on wire_trace as before, post one voucher, send the new log. Expect "unpark hit" lines on reopened indexes and roughly 50-60 fewer round trips than the mtfix8 run (about 2.5s at your ~43ms latency), mostly from dropped GotoTop frames.
- Also run a normal startup, a reindex, and two or three Vouch instances at once.

## Packages
- openads-1.09.68-mtfix9-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
