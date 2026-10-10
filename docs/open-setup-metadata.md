# Capability-gated remote open metadata

New clients advertise kCapOpenSetupMetadata. A supporting server appends the
immutable record length and, when a production bag opens successfully, its
ordinary OpenIndexAck payload to the existing OpenTableAck TLV sections.
Clients without that capability get the existing reply format unchanged.

The server reuses its existing OpenIndex handler, including session-local
bindings and index-open rollback. It restores natural order after the setup.
The client parses index metadata through the same parser as an ordinary index
reply, registers it through its existing AdsOpenIndex path and still checks the
caller's output-array capacity. Record length warms the existing handle cache.
Missing or rejected metadata uses the existing wire fallback.

Before consuming the warm index reply, the client checks the requested bag's
case-insensitive stem against the confirmed production bag. This follows the
existing production-bag dedup convention, including CDX/Z01 spellings. The
production path is set before the first auto-open, so the guard can run there.

Tests cover CDX and Z01 production bags, initial natural position, explicit
same-bag open, record-length cache, independent login cursors, CloseAll/reopen,
legacy capability gating, and short mandatory index payloads. The per-USE
opcode counters show no separate GetRecordLength or OpenIndex request for a
valid warmed production bag. Server-side setup work still occurs within open;
this is an RTT saving, not proof that all CPU or elapsed startup time improves.

No count-cache policy, OPENADS_FRESH_COUNTS, daemon-hardening exemption,
Harbour RDD-core change, lock change, data-jail change or CI change is included.
