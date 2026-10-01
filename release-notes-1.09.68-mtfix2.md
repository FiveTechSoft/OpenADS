# OpenADS test build 1.09.68-mtfix2 (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on 1.09.68. Not an official release.

## What's in it
- Session-pool multi-thread fix 1: the remote table store (park/adopt on close, AdsCheckExistence scan, index-park invalidation, relation check) now always holds the global lock. Fixes a heap corruption crash seen in network_pool_mt_test.
- Server multi-thread fix 2: conn_for_table holds the global lock while scanning connection table maps (ThreadSanitizer race with Connection::open_table).
- Logical fields over remote (contributor patch): remote writes now store the DBF byte 'T'/'F' instead of '1'/'0', so FOR-conditional indexes, DBFCDX and SAP ADS read them correctly. New regression test abi_remote_logical_write_test.
- Patch 1: safe local answers on the remote path (fewer round trips at startup).
- Wire-frame logging kept on for diagnosis.
- network_pool_mt_test repeats its 4-thread phase 25 times; new network_pool_mt_repeat test runs it in 30 fresh processes.

## What to test
- Several Vouch instances at once, many threads, against openads_serverd from this build (Linux tarball) and the client DLL from the Windows x86 zip.
- LOGGED turns T on login; the license limit holds.
- Startup time over remote.

## Packages
- openads-1.09.68-mtfix2-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
