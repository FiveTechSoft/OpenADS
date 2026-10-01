# OpenADS test build 1.09.68-mtfix5 (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on 1.09.68. Not an official release.

## What's in it
- Everything in 1.09.68-mtfix4 (session-pool MT fixes 1 and 2, contributor logical-field fix, patch 1 safe local answers, patch 2 still-empty window, remote REINDEX fix).
- MT fix 3: a remote table open no longer holds the process-wide OpenADS lock while it waits for the server's reply. Before, every other OpenADS call in the same process (any thread) waited a full round trip whenever one thread opened a remote table, and with an in-process server it could deadlock (rare hang in the repeated pool MT test). Same rule the code already used for the pool flush and the production-index auto-open.
- Wire-frame logging kept on for diagnosis.
- network_pool_mt_test (25x 4-thread phase) and network_pool_mt_repeat (30 fresh processes) pass; a separate diagnostic loop ran the pool MT test 1,800 more times per build with no hang.

## What to test
- Several Vouch instances at once, many threads, against openads_serverd from this build (Linux tarball) and the client DLL from the Windows x86 zip.
- REINDEX over remote: .z01 file times should change.
- Startup time over remote.

## Packages
- openads-1.09.68-mtfix5-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
