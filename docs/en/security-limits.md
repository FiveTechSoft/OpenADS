# Network security limits

Before authentication: 64 KiB per frame, 30-second handshake. After Connect:
16 MiB per frame. Idle connections expire after 5 minutes and partial frames
after 30 seconds. Reconnects, malformed capabilities and embedded NUL fail.
Limits per session: 256 tables/cursors, 64 created mutexes, 4096 explicit/AppendBlank locks.
Remote mutex contention fails immediately instead of occupying a worker.
SetFields checks count before allocation and accepts at most 2048 fields.

Fetch/FetchWhere reject fields over 65535 bytes and replies over 16 MiB instead
of truncating wire lengths. FetchWhere errors after examining 100000 rows per
request; use selective SQL or smaller pages. Management telemetry does not
retain SQL text because statements may contain credentials or other secrets.

MgConnect now checks server credentials, including the password that older
clients ignored. Without credentials only loopback read-only management is
allowed. Management mutators require authentication. Older clients must update
to administer a credential-protected server. TLS is still required for secrecy
and cryptographic replay protection. SQL-generated append locks and other SQL resource surfaces remain under review.
