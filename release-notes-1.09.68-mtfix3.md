# OpenADS test build 1.09.68-mtfix3 (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on 1.09.68. Not an official release.

## What's in it
- Everything in 1.09.68-mtfix2 (session-pool MT fixes 1 and 2, contributor logical-field fix, patch 1 safe local answers).
- Patch 2: still-empty window. After the server confirms a table is empty, Seek / GO n on that table are answered locally for up to 1.5 s instead of a round trip each. A new record added by another station shows up once the window ends; this station's own writes close the window at once. OPENADS_EMPTY_TTL_MS sets the window in ms (0 disables, max 2000). Trace lines mark each local empty answer.
- Wire-frame logging kept on for diagnosis.
- network_pool_mt_test (25x 4-thread phase) and network_pool_mt_repeat (30 fresh processes) pass.

## What to test
- Several Vouch instances at once, many threads, against openads_serverd from this build (Linux tarball) and the client DLL from the Windows x86 zip.
- Startup time over remote compared with mtfix2.

## Packages
- openads-1.09.68-mtfix3-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
