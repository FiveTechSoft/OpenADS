# Daemon session lifetime

Reported by Pritpal Bedi during mtfix40 ERP invoice testing: after a tea
break, idle modules could no longer use their database sessions. The daemon
had treated five minutes without application bytes as a dead connection.

With mtfix41, a successfully established database session has no
application-inactivity deadline. Reading a screen, taking a call or leaving
an invoice module open must not release its physical record/login locks.
The same policy applies in the dedicated-thread and reactor-pool paths,
including TLS. This upstream port preserves upstream's unconditional
handshake and frame safety timers rather than adding the fork's separate
daemon-hardening gate. Established DBF inactivity is disabled for every
network Server by default, including an embedded network listener. True
LOCAL DBF access is not a network session. Client DLL behavior and wire
format are unchanged by this idle-policy change.

The daemon still closes unfinished handshakes after 30 seconds, stalled
partial frames after 30 seconds, and undrained replies after 30 seconds.
Management sessions keep their five-minute inactivity limit. Database
sessions close on explicit disconnect, EOF/reset/transport failure,
admin removal or server shutdown. Accepted-socket TCP keepalive remains
in place to detect unreachable peers; its probes are not application bytes.
TCP acknowledgement cannot distinguish a wedged application from an idle
person, and socket-option calls currently do not report tuning failures.

Finite session admission limits bound occupied slots. Do not evict healthy
idle lock owners to admit new users. This fix does not address burst queued
admission accounting, total memory pressure, OOM, or client recovery after
a genuine connection loss. It is not an unconditional crash guarantee.
No reconnect, lock resurrection, write replay or commit replay is added.

Regression coverage uses an injected steady clock configured before start,
with dedicated/pool idle resume and lock ownership tests, abrupt peer
cleanup, preserved handshake/management/partial-frame clocks and TLS idle
resume. CTest also runs a real 310-second dedicated-mode idle soak. The
clock has no runtime configuration or protocol surface; ordinary execution
uses the steady clock.

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
