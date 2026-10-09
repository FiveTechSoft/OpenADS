# Regenerated scoped mtfix39-41 upstream port

Base: FiveTechSoft/OpenADS main 91a63f6e7ac90d4f0b64d7cdafecaabb1548b140 (1.09.73).
Private review preparation for Pritpal Bedi. No public branch, PR or package.

## Scope and adaptation

Fresh current-row contents returned in a negotiated LockRecord acknowledgement; older peers fall back to a real post-lock refresh, retaining acquired-lock ownership before a refresh can fail. Remote file waits release the process-wide registry mutex while per-file serialization and close-race lifetime are retained. Session-thread launch failure rejects the connection without terminating the accept loop.

Established DBF sessions keep their locks during healthy idle. Optional INI/CLI established_session_idle_seconds defaults to 0. Handshake, partial-frame, reply-drain and management timers remain. Regression tests and historical fork notes credit Pritpal Bedi's invoice, multithread and idle-session testing.

No .github/CI/workflow change, OPENADS_FRESH_COUNTS cached-count policy, earlier metadata setup feature, daemon-hardening gate or later ephemeral TLS-fixture cleanup is imported. Only kCapLockedRow is added, not kCapOpenSetupMetadata. No reconnect/replay or OOM survival guarantee.

Upstream's lifecycle safety checks remain unconditional. Unlike the fork, this port has no separate daemon-hardening/embedded-listener exemption: established DBF inactivity is disabled for embedded network listeners too. Unfinished-handshake expiry remains. True LOCAL DBF access is not a network session. The existing upstream static localhost TLS fixture is unchanged; no key is added.

## Regeneration provenance

The original private cb00b767 build and attachments were lost when the temporary workspace was replaced. This port was reconstructed from the same upstream base, the same seven original commits and the recorded conflict-resolution design. It is a regenerated artifact, not recovered bytes. No intended production/test behavior or scope change was made. Byte-for-byte identity to the lost tree cannot be established. This note differs to identify regeneration and record fresh tests; commit identity and timestamps also differ. Historical fork milestone notes remain explicitly labeled as history, not validation of this port.

## Fresh local validation

Linux GCC Debug, SQLite ON, HTTP OFF, O0/g0, single-threaded builds:

- Normal and TLS unit-test, serverd and CDX benchmark targets built.
- TLS focused regressions: 19 cases / 1,036 assertions passed. Normal: 18 cases / 1,021 assertions passed.
- Fast-suite bounded ranges completed 1,684 unique TLS cases and 1,681 unique normal cases without assertion failures. Initial bounded commands hit their time cap; interrupted cases were rerun successfully. Executed case names/durations were counted independently because doctest range summaries count all filter matches, not only executed cases.
- Slow CTest, SQL URI, SQLite filter/seek and 30 pool-MT repeat CTests passed in both builds.
- CDX 1,000-row benchmark smoke passed in both builds.
- Strict -Werror syntax checks passed changed network implementation units, changed/new test units and config_ini.cpp. Existing shadow warnings in ace_exports.cpp/serverd/main.cpp failed strict GCC checks both on this port and pristine upstream. Warnings-as-errors was disabled only in local CMake caches, never repository policy.

Still owed: uninterrupted whole CTest including the real 310-second idle soak; actual CI preset/HTTP configuration; Windows x64/x86, macOS, PHP/Harbour and live external SQL backends. No hosted CI has run for this private regenerated port. Fork application soak/CI results do not establish that this upstream-specific source is ready for release.

## Review title

Port mtfix39-41: fresh locked rows, remote file waits and healthy idle sessions

The user reviews the final content before any public branch/PR step and creates the FiveTechSoft PR himself. The existing fork mtfix41 branch must not be selected directly: it carries 85 commits/110 files against this upstream base, including excluded earlier work.
