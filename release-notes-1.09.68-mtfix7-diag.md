# OpenADS test build 1.09.68-mtfix7-diag (TEST ONLY - not for production)

Branch: perf/safe-local-answers on bedipritpal/OpenADS. Based on mtfix6 / 1.09.68. Not an official release. Nothing merged to main or upstream.

## What's in it
- Everything in 1.09.68-mtfix6, plus a client-only timing trace for remote operations. No Vouch application change or server update is needed for this trace.
- Opt-in `wire_trace = 1` in the `openads.ini` used by Vouch. Set `wire_trace_file = C:/tmp/cli_trace.log` to choose the output path; create `C:/tmp` first. If unset, this is the default path. Or set `OPENADS_WIRE_TRACE=1` and `OPENADS_WIRE_TRACE_FILE` before starting Vouch. Restart Vouch after changing the settings; turn tracing off again when done.
- Each completed remote round trip records a local wall-clock timestamp, milliseconds since trace start, table alias when known, named wire operation (AppendBlank, SetFields, FlushTable, LockRecord, UnlockRecord, Seek, etc.), request/reply sizes, duration in microseconds, table ID, and connection marker. Existing local-answer trace lines remain. Field values and seek keys are not logged by this patch. The log can contain file/table names and existing trace details, so review it before sharing. Tracing adds disk I/O and changes timings: use it to count calls and find slow calls, not as an unmodified benchmark.

## What to test
- Post one voucher over the remote connection. Compare the voucher append/write/commit/unlock and the two GL seek/lock/write/commit/unlock paths. See whether field writes go as a single SetFields batch or multiple SetField frames.
- If useful, run one remote startup with tracing on. Zip and share the log after reviewing it. No timing points need to be added in Vouch.

## Packages
- openads-1.09.68-mtfix7-diag-windows-x86.zip, windows-x64.zip, linux-x64.tar.gz, macos-universal.tar.gz
