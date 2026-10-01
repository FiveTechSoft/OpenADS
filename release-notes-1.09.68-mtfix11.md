Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix11 - two ADS-compatibility fixes

### 1. AdsIsRecordLocked now sees other connections' locks
Previously it answered from the caller's own lock list only (per connection), so a guard that QUERIES a marker record instead of attempting a lock stayed blind to other instances. Now: own list first, then a real OS byte-range lock probe (F_OFD_GETLK with F_GETLK fallback on Linux/macOS, try-LockFileEx on Windows). CDX/VFP nuance handled: a file lock's byte range covers every record lock, so another owner's FLock is correctly reported for any record, without false positives. SAP's own doc text for AdsIsRecordLocked was not independently re-verified - the behavioral evidence from production traces is the compat signal here.

### 2. ADS_EXCLUSIVE opens enforced across sessions
Exclusive opens were a silent no-op (POSIX has no share modes), so a second instance's opens sailed through an exclusive hold that real ADS denies. The server now keeps an open registry keyed by the resolved file path: an exclusive holder denies other sessions both shared and exclusive opens; a shared-opened table denies another session's exclusive open; reopening/upgrading on the SAME connection stays legal. Denied opens return 7040 AE_FILE_IN_USE. Close and disconnect release the registration. Wire-level only: SQL-internal and engine-internal opens bypass it (same as before). Note for our own client: a closed shared table may stay parked for fast reopen, which keeps its server registration until eviction or disconnect - documented in the new test.

### Tests
Full CI-tier suite green: 1579/1579 cases, 560,890 assertions. New: M13.1 cross-connection IsRecordLocked (record lock, FLock visibility, unlock-to-free), mtfix11 exclusive-open matrix (denial both directions, same-session reopen, upgrade rules, teardown sweep). One pre-existing test helper fixed (it had been opening ADS_EXCLUSIVE under the name open_shared - masked while exclusive was a no-op).
