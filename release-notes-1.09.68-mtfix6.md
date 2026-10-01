# OpenADS test build 1.09.68-mtfix6 (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on 1.09.68. Not an official release.

## What's in it
- Everything in 1.09.68-mtfix5 (session-pool MT fixes 1-3, contributor logical-field fix, patch 1 safe local answers, patch 2 still-empty window, remote REINDEX fix).
- Single-attempt locks by default. A failed AdsLockRecord / AdsLockTable now returns AE_LOCK_FAILED after ONE attempt (one round trip remote), matching xBase RLOCK()/FLOCK() semantics - fail means fail and the application owns any retry loop. Before, the ACE client kept retrying a contended lock for up to ~1 second (10 x 100 ms) even though the server had already answered on the first attempt, which turned every intentional lock probe (login-semaphore walks, "already logged?" checks) into a ~1 s burn per probe. Opt back into ACE-style waits per install: openads.ini lock_retry_count = 10 (env OPENADS_LOCK_RETRY_COUNT), optional lock_cycle_ms ceiling (default 100). Apps that call AdsSetLockCycle / AdsSetLockRetryCount get exactly what they ask for, unchanged.
- The server's append auto-lock keeps its own internal budget (10 x 100 ms, engine_lock_retry_count / engine_lock_cycle_ms) - it is an internal wait the app cannot do itself, so single-attempt client locks do not break appends under FLock/Browse convoys.
- Lane pinning by alias (lane_pin_alias = 1, default). Every USE of the same alias+path on one logical connection binds to the same server session, from any thread and across close/reopen. Record-lock identity now follows the workarea, so Harbour zero-space workareas (hb_dbRequest / hb_dbDetach, Vouch's LOGIN_A pattern) keep their locks visible and unlockable no matter which thread holds the workarea. Different aliases of the same file keep different lock owners, so cross-owner lock probes (UsrConsole) still conflict as before. lane_pin_alias = 0 restores pure thread-affinity.

## What to test
- Login semaphore flow from several threads: log in on one thread, IsLogged()/__howManyLogins from others; lock probes should be instant now, not ~1 s each.
- b_devboo / b_backup paths (LogUse closes and re-USEs LOGIN_A): login state must stay consistent afterwards.
- UsrConsole: still shows logged-in users (lock probes across aliases must still fail).
- Saving a record over remote: RLock -> REPLACE -> Unlock should cost only its round trips (about 4-6).
- Startup time over remote; several Vouch instances at once against openads_serverd from this build (Linux tarball) with the client DLL from the Windows x86 zip.

## Packages
- openads-1.09.68-mtfix6-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
