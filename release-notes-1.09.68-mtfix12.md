Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix12 - navigation fusion: boundary pairs, self-goto suppression, record-number anchoring

One voucher save replayed from the mtfix11 production trace: 893 wire round trips. This release takes 155 of them off the wire in the default configuration (-17%), up to 201 (-23%) with the opt-in below. At the WAN rate he measured (~40 ms/RTT) that is about 6 s off the 38.5 s run by default, about 8.5 s with the opt-in. Zero behavior change: every suppressed call answers from state the server already certified, under the same freshness envelope the shipped duplicate-suppression uses.

### 1. Boundary-pair certification (protocol extension, caps-gated)
GotoTop and GotoBottom requests can now ask the server to certify BOTH boundaries in one visit (new capability bit kCapNavBoundaryPair; old clients and old servers interoperate unchanged - the byte is only sent when both sides advertise it). The app's dbGoTop(); dbGoBottom() ritual - 102 adjacent pairs in the trace - now costs one frame instead of two. Bonus: top and bottom read in the same server visit are more consistent with each other than two separate frames ever were.

### 2. Self-GotoRecord suppression
Re-issuing GotoRecord for the record the server cursor already sits on (the xBrowse bookmark-restore pattern - 89 of 126 GotoRecords in the trace are self-gotos) now answers locally. The default envelope is connection-wide (53 served in the replay); setting OPENADS_NAV_SELF_GOTO=table widens freshness to per-table (96 served in the replay) for apps that want the extra savings.

### 3. GetRecordNum from the certified cursor
AdsGetRecordNum answers from the server-certified cursor anchor when the freshness envelope holds, instead of asking the server where it already told us it is.

### Validation
- Full unit suite: 1588/1591 green; the 3 failures are the known pre-existing flaky tests that fail identically on the clean tree (mt contention, openindex race, create storm).
- New boundary-pair unit tests (82 assertions): pair serve in both directions, write invalidation, cross-table expiry, self-goto conditions, record-number anchor.
- Analytical replay of the mtfix11 production trace with the shipped invalidation rules: -102 (boundary pairs), -53 default / -96 opt-in (self-goto), GetRecordNum serves on top. Predicted wire totals: 738 default, 692 with OPENADS_NAV_SELF_GOTO=table (from 893).
- The replay caught one real bug during validation: the self-goto serve initially kept the prefetch queue a wire GotoRecord would have cleared; fixed, covered by the ramp test.

### A/B
Same harness as mtfix9-11: wire_trace=1 in openads.ini (or OPENADS_WIRE_TRACE=1), OPENADS_WIRE_TRACE_FILE picks the log path. Run the voucher save on the mtfix11 build, then on mtfix12, count ` wire ` lines. Expect ~17% fewer by default, ~23% with OPENADS_NAV_SELF_GOTO=table; zero errors either way.
