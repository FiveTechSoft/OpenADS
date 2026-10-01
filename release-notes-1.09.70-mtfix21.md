Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.70-mtfix21 - upstream-sync and Exclusive ADT append test build

This is a test build, not a production recommendation. Keep mtfix15 as the recommended working build until Pritpal checks this candidate in Vouch. Use matching client and server builds. Back up the working binaries before testing.

This candidate syncs FiveTechSoft/OpenADS main through 54e237b1 into the fork's test branch with a real two-parent merge. It keeps the fork's mtfix20 lock-contract fixes, client lock-ledger fast paths, engine append retry policy, and CI/release workflows. Upstream's accidental duplicated table.cpp prefix was removed locally so the merged source has one append implementation. No source merge to fork main or upstream is part of this release.

### Exclusive ADT append

Exclusive ADT appends no longer add an automatic record lock for each new row. Shared ADT appends still take a record lock, and shared writes still require the caller's lock. The engine change affects Exclusive ADT engine instances, including the server; it is not limited to LOCAL connections. DBF append behavior is unchanged by this condition.

A new LOCAL ADT regression appends 10,000 rows with field updates and no per-row AdsWriteRecord, checks that Exclusive mode retains zero record locks, closes/reopens the table, and verifies distinct persisted values at rows 1, 5,000 and 10,000. It also checks the Shared write guard and append lock/unlock behavior.

No measured speed gain on Tim Stone's 300,000-row program or on Pritpal's Vouch workload is claimed. Harbour's ordinary DbUnlock can skip Exclusive tables; direct AdsUnlockRecord reaches the engine. The shown append loop does not establish per-row fsync, and an empty transaction log without an explicit transaction is expected.

### Other synced upstream work

The upstream base advances CMake's project version to 1.09.70. The test series steps up from v1.09.68-mtfix20 to v1.09.70-mtfix21 to match that base; use the new version in test download scripts. The tag supplies the packaged binary version. The sync also includes upstream NTX inter-process index locking and MariaDB numeric/logical setters. Those upstream additions need application testing where used.

The server-side own-lock query recognizes the calling engine handle's table lock and the ABI twin's record locks, with an engine held-list fallback. It does not substitute a global other-connection lock probe for the caller's own lock state.

### Validation

Local Linux GCC validation used -O0/-g0 and disabled warnings-as-errors after existing shadow warnings. The focused lock/introspection harness passed 50 cases and 2,120 assertions. The new ADT regression passed 1 case and 20,034 assertions.

The full local suite ran 1,604 cases: 1,601 passed, 3 failed, and 16 were skipped, with 598,135 assertions and 8 assertion failures. The failures involved the MT reader/appender walk, an Exclusive-held OpenIndex return-code expectation, and remote create/append stress counts. All three failure classes reproduced on a reconstructed pre-sync 807f0694 baseline with the changed core sources and their header consumers recompiled. This was a focused baseline comparison, not a fresh full baseline CI run; it does not make the full suite green.

CI #41 on 510e3626 passed all 10 non-macOS jobs, including Windows MSVC x86/x64, Linux with and without TLS, Harbour smoke, PHP, and the live SQL jobs. macOS configured and built successfully, then its unit test was canceled by the existing 30-minute CI job cap. macOS unit tests remain unverified; the overall CI status is Cancelled, not green. The release workflow separately requires all four packaging legs and canonical assets before publication.

CI: https://github.com/bedipritpal/OpenADS/actions/runs/36664053398

### Application checks

Test Tim's Exclusive LOCAL ADT append program against a backed-up dataset and compare elapsed time, record count, persisted values, and lock count. Test Shared append and locked record edits separately. For Vouch, retest repeated RLOCK/unlock, DBRI_LOCKED versus dbrLockList(), blocked edits, and LOGIN_A using matching client/server binaries. Record exact return codes, handles and record numbers if anything disagrees. No Vouch result is claimed here.
