# OpenADS test build test-p1-mtfix-1 (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Not an official release.

## What's in it
- Session-pool multi-thread fix 1: remote table store (park/adopt on close, AdsCheckExistence scan, index-park invalidation, relation check) now always holds the global lock. Fixes a heap corruption crash seen in network_pool_mt_test.
- Server multi-thread fix 2: conn_for_table holds the global lock while scanning connection table maps (ThreadSanitizer race with Connection::open_table).
- Patch 1: safe local answers on the remote path (fewer round trips at startup).
- Wire-frame logging kept on for diagnosis.
- network_pool_mt_test repeats its 4-thread phase 25 times; new network_pool_mt_repeat test runs it in 30 fresh processes.

## What to test
- Several Vouch instances at once, many threads, against openads_serverd from this build (Linux tarball) and the client DLL from the Windows x86 zip.

## Packages
- openads-test-p1-mtfix-1-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
