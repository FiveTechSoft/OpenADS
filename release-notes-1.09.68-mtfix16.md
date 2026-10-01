Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix16 - opt-in fresh-organization diagnostics

This is a test build, not a fix or a production recommendation. It keeps mtfix15 behavior, but can log stages for remote table creation and subsequent server-side opens and appends. The server records DBF write, memo creation, its local post-create reopen, close result, wire OpenTable outcomes, and AppendBlank failures. The client records whether CreateTable failed on the wire or on its own final reopen, and failed OpenTable and AppendRecord calls. Codes are not changed. No table schema or row content is written to this diagnostic log; it contains sanitized table basenames, stage, code and timestamp. The normal `ads_err` log remains separate and may hold additional context.

Set environment variables on **both** the Vouch client process and the server process before launch:

- `OPENADS_CREATE_TABLE_DIAG_FILE`: a distinct writable log path on each machine, e.g. `C:\temp\openads-create-client.log` and `/tmp/openads-create-server.log`. Absent means diagnostic off.
- `OPENADS_CREATE_TABLE_DIAG_FILTER=uxxyyzza.dbf` for one table. To investigate several first-login table failures, omit FILTER: every CreateTable and failed OpenTable or AppendBlank can be logged. This broad mode can cause extra disk I/O and stops writing at 2 MB per process; use it for one fresh-org reproduction only, then unset both variables and restart. With a filter, the server additionally logs successful opens and first appends of the named table.

Keep client/server DLL and daemon from this same build. Collect both logs, the app's exact error or silent symptom, and the table name and time. A create error followed by a file on disk proves the file was written but not where the subsequent failure happened; the stage log is the answer. The test uses a long-path fixture on POSIX and a memo-create failure on Windows to force a post-write failure, then checks that the DBF remains, the stage is logged, and the same ACE code reaches the caller. This diagnostic does not modify locks, retry policy, lane pinning, file format, or client-visible error codes.

**CI caveat:** Windows x86/x64 and Linux tests, including the new diagnostic test, passed on this commit. macOS compiled, but its full test step exceeded the CI job's 30-minute limit and was cancelled; the same macOS timeout occurred on the prior diagnostic commit and predated mtfix16 on this branch. The release workflow does not use macOS tests as a gate. macOS test status remains unverified. Keep mtfix15 as the recommended working build; use mtfix16 only to capture the fresh-org failure, then restore mtfix15.
