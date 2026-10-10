# Aggregate health backend

`OAdsGetServerStats(hMgmt, json, len)` is an additive OpenADS C extension.
Use an `AdsMgConnect` management handle, not a data connection handle.
No existing ADS management structure or serialized snapshot format changes.

`*len` is the supplied capacity in bytes, including the NUL terminator. On a
successful query it becomes the required size including NUL. A NULL or short
buffer returns `AE_INSUFFICIENT_BUFFER` and writes no partial JSON. Allocate
headroom or retry when the next sample has grown. An invalid handle returns
`AE_INVALID_CONNECTION_HANDLE`; a NULL length pointer is an error.

Local management handles sample the calling process (`scope: local_process`).
Remote handles sample the server (`scope: server`) through the additive
`MgRequestKind::HealthJson` read request. `AdsMgConnect` accepts host:port and
`tcp://host:port`; management uses plain TCP, not TLS. Existing management
credentials are sent on each management connection. Do not treat a TLS-enabled
data server build as encrypted management transport.

The existing management handshake and credential policy remain in force.
Configured credentials are checked. Without configured credentials, the
existing loopback-only read-only management session may read health; it still
cannot run management mutators. No handshake means no health read. An older
server that rejects the new request returns `AE_FUNCTION_NOT_AVAILABLE`.
Transport failures return `AE_NO_CONNECTION`; later handshake refusal returns
`AE_ACCESS_DENIED`. The client bounds replies to 65,535 bytes and checks an
object envelope, not full JSON syntax validation.

## Schema 1

The JSON contains aggregates, never usernames, client addresses, SQL text,
credentials, or table/index path strings. Distinct path counts use exact
stored strings, not canonical filesystem identity.

- `users`, `connections`, `workareas`, `table_handles`, `index_bindings`,
  `locks`: `current`, `max_used`, and `rejected` (unknown, null).
- `worker_threads`: current existing server thread telemetry; peak and rejects
  are unknown, null. This is not a claim of process-wide OS thread counting.
- `distinct_table_paths`, `distinct_index_paths`, `uptime_seconds`,
  `operations`, `logged_errors`, `rss_bytes`, `server_port`.
- `max_sessions` and `max_sessions_source`: resolved server admission cap and
  its source (default, environment, INI, command line, or API override).
  Explicit server zero means unlimited. Both fields are null for a local
  process sample. The default remains 500.
- `packets_in`, `packets_out`, `bytes_in`, `bytes_out`, `disconnects`,
  `partial_connects`: existing process telemetry, with wide integer output.
- `os_fd_count` and `os_fd_soft_limit`: Linux process measurements when
  available; null on other platforms or when unavailable/unlimited.
- `parked_handles`: null. Parking is client-only state.
- `semantics`: descriptions carried in every sample.

Workareas mean server-open table handles, including handles parked at the
client. They are not Harbour `Select()` areas and not OS file descriptors.
Workarea/table-handle maxima retain open events, including opens that close
before a dashboard sample. Other maxima keep the existing telemetry semantics.
Maxima are process-global; samples from multiple embedded Server instances
share them. Resource counts retain existing 32-bit widths; byte counters and
other wide counters do not truncate to 32 bits. A snapshot is a best-effort
concurrent sample; the management query session itself may appear in counts.

## Scope

This backend introduces no Harbour wrapper, portal/dashboard consumer,
listener, web endpoint, lock algorithm, count-cache policy, or daemon safeguard
exemption. The C extension is exported through the existing shared-library
export paths, including the Windows x86 stdcall wrapper.
