# Application-controlled diagnostics

The master is OFF by default. No environment or INI entry can enable the
client master. Existing Harbour binding: `OAds_SetLogging(lOn) -> lOk`.
C API: `OAdsSetLogging(UNSIGNED16 usOn) -> ACE status`.

The application must explicitly call the existing API with true in the
same process to permit diagnostic output. Calling false blocks diagnostic
output again, including an already-installed wire hook. A separate helper
process cannot authorize an already-running client. No Vouch behavior or
startup-option change is implied by this API.

Once the application permits diagnostics, the existing env/INI selectors
choose channels as before. Client wire selector `OPENADS_WIRE_TRACE=1`
(or `wire_trace=1`), destination `OPENADS_WIRE_TRACE_FILE` (or
`wire_trace_file`). Default destination remains `C:/tmp/cli_trace.log`;
its parent directory must exist. Selection and file destination are cached,
so restart after changing them. The application master itself is dynamic.

The standalone server uses separate explicit admin-owned server config:
`diagnostics=true`, default false. The environment does not set this master.
Config-file/service permissions must restrict who can edit it. Restart to
apply server config. Client tracing does not require server tracing.

Trace lines retain operation, byte counts, durations and safe table IDs.
Real paths/names, filename-derived aliases, index expressions, SQL and raw
error text are not persisted. Distinct explicit aliases remain visible.
Auth proof frames and request/reply payloads are excluded. API errors still
return their normal text to the application. Database recovery journals
are data, not diagnostics, and are unchanged.

The file is appended, not cleared or rotated by the master. Existing raw
files from older versions are not sanitized by an update. Capture briefly,
turn the application master off, and review files before sharing. Trace
writes add overhead; traced wall-clock time is not a clean speed benchmark.

Operational CLI responses remain: help/version, readiness/shutdown and safe
startup/service failure status. The master gates diagnostics, not these
management responses. Sensitive startup paths/raw errors are suppressed.
OPDUMP's environment selector cannot start sampling while the master is OFF.

## mtfix69 trace columns

`cli_trace.log` uses plain fixed-width columns separated by two spaces,
following the Vouch DBF_ADS log style. The elapsed column is a bare
millisecond number, with no plus sign or repeated ms suffix. Frame
`dur_us` remains microseconds, unchanged. Only actual completed frame-hook
rows carry `W`; every API summary, local call and context row has a blank
flag. Compact mode (`wire_trace_compact=1`) prints only those frame rows.
Full mode keeps the additional blank-flag rows. No A or L flags are printed.
The full and compact frame sequence is the same; formatting changes do not
remove requests. Existing masking and frame-coverage limits are unchanged:
only completed request/reply pairs through RemoteConnection::request are
covered; failed/incomplete pairs and separate auth transport are not a total
network-traffic census.

## mtfix73 ACE caller column

One 48-character fixed-width column follows the W/blank flag. Completed
frames show the outer synchronous ACE entry API plus a static dispatch
reason, for example AdsWriteRecord/batch+commit,
AdsUnlockTable/buffered-sets or AdsGotoBottom/internal-keycount.
Request is the default reason: it does not claim a literal direct application
call. Summary rows leave this column as a dash. Uninstrumented/direct
transport callers show unknown, never a previous API's label.

The detail includes process-local call and logical thread correlation IDs,
and the currently executing nested ACE API. Nested calls retain their
outer trigger's call ID; return and exception unwind restore prior context.
IDs can have gaps, are not record IDs, and are not stable across process
restarts or separate DLL copies. The IDs do not establish event ordering
between threads. A batch labels what triggered its dispatch, not where its
buffered assignments originated. Synchronous same-thread dispatch is covered;
background/cross-thread work needs explicit context or remains unknown.

This does not identify a Vouch PRG function/line, an RDD source call or a
native call stack, and cannot by itself distinguish application from RDD.
No stack walking, source paths, arguments or payloads are added. Existing
masking, master OFF/default, opt-in and completed-frame limits still apply.
All 684 public entries in ace_exports.cpp are scoped; other uninstrumented
entry surfaces are not claimed covered. Disabled entries do no ID allocation
or scope-stack update; the master/selector checks still have a cost. Enabled
entries add TLS context, ID allocation on outer entries and longer log rows.
No zero-overhead or production speed claim. No wire/protocol/server storage,
Vouch source, commit, lock, navigation or count policy changes.

## Application boundary markers (mtfix76)

`OAds_TraceMark(cLabel)` writes a client-local M row into cli_trace.log, in BOTH
full and compact modes. It requires wire_trace and the master gate; it never
enables them. No connection or RDD action occurs. The label is 1..120 bytes with
no C0/DEL/C1 control bytes. Use ASCII developer labels, never private data.
The result is diagnostic only, not application-save status. The executable contracts are in tests/trace_marker_contract.py and
tests/trace_marker_harbour.py.
Count only W rows as wire frames. M rows have no wire duration and advance the
previous-row gap column. Thread IDs and steady tick_us pair application scopes;
local file writes have overhead and markers are not cross-thread wire barriers.

## Optional instance/thread IDs (mtfix77)

The SAME Harbour function accepts `OAds_TraceMark(cMark [,nInstId [,nThreadId]])`.
Omitted IDs are 0; explicit NIL/type/range errors return .F. Exact integers accept
0..9999999999999999; floats must be finite/integral and <=9007199254740991.
Both flavors have right-aligned instance/thread fields, then marker, with
separate auto_thread and tick_us. The old C ABI remains; an additive Ex bridge
carries IDs. The numeric acceptance cases are in tests/trace_marker_numeric_test.c.
