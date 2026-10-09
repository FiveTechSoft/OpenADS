# OpenADS 1.09.73-mtfix41 - healthy daemon idle sessions

> Historical fork test-build notes. This document describes the original bedipritpal/OpenADS build, not validation or packaging of the upstream port. See docs/upstream-mtfix39-41-port.md for port scope and validation.

TEST BUILD ONLY. Based on mtfix40 on an isolated branch of
Pritpal Bedi's fork of FiveTechSoft/OpenADS. Nothing merged
to main or upstream. Application test clearance is still required.

Thanks to Pritpal Bedi for the invoice logs and tea-break idle-session
report that exposed this failure.

## Change

The daemon no longer expires an established database session solely because
it has received no application bytes for five minutes. A healthy ERP module
can remain idle without losing its workareas or physical login/record locks.
Both dedicated and reactor scheduling use the same policy, including TLS.

The 30-second handshake, partial-frame and reply-drain protections remain.
Management sessions retain their five-minute idle limit. Real disconnects,
EOF/reset/transport failure, admin removal and shutdown still clean up
session resources and locks. Existing server-side TCP keepalive remains.

No client request code, Vouch code, local/embedded policy or protocol format
changes. Packages carry the test-build version stamp; the fix is server-side.

## Validation and limits

Tests cover simulated 45-minute healthy idle/resume with login-lock
exclusion in dedicated and pool modes, abrupt dead-peer cleanup/reacquire,
retained security timers, TLS idle resume, and a real 310-second idle soak.
Source CI passed Windows x86/x64, macOS, Linux normal/TLS, Harbour/PHP
and live SQL jobs: https://github.com/bedipritpal/OpenADS/actions/runs/37870693758.
The existing ordered-prefetch traffic-counter test failed once in the TLS
job (hot_bytes was 0), then passed the single retry on identical source.
The original failure remains in CI history; no assertion was weakened.

This does not fix client error-dialog behavior after a genuine network loss,
add automatic reconnect/replay, or guarantee survival of host OOM/SIGKILL.
Memory-pressure admission work remains separate.

## Application check

Keep the mtfix40 client unchanged. Update only the test server, leave Vouch
and its invoice module idle beyond five minutes, then resume normal work.
Check that a second instance cannot take the still-held login lock. Test
normal disconnect and closing modules too. Do not use in production yet.

## Optional daemon idle policy

`established_session_idle_seconds = 0` in the SERVER openads.ini is the
safe default: no established database-session idle cutoff. A positive value
opts into a cutoff in seconds, for example 3600 for one hour. The matching
`--established_session_idle_seconds N` CLI flag overrides the INI value.
Restart the server after changing it. Negative, malformed and overflowing
values are rejected. No OAds_Settings/client DLL change is required.

Enabling this deliberately disconnects inactive database users and releases
their workareas/locks. It is not a test for a dead transport. Use it only
when that administrative policy is intended. Management and stalled-frame
security timers are separate and unchanged.
