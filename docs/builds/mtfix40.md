# mtfix40 test build

> Historical fork test-build notes. This document describes the original bedipritpal/OpenADS build, not validation or packaging of the upstream port. See docs/upstream-mtfix39-41-port.md for port scope and validation.

Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.
Pritpal supplied the B_BIG source, remote frame trace, client-storm crash report and application testing that led to this build.

This is an isolated test build, not a production recommendation. No fork-main or upstream merge is part of it. Back up data and binaries, and use matching client and server versions.

## Changes

- Remote AdsFOpen, AdsFCreate, AdsFRead, AdsFWrite, AdsFSeek and AdsFClose no longer hold the process-wide registry mutex while waiting on a remote connection. Per-file operations remain serialized; retained ownership keeps the file record alive during close races. Disconnect still serializes with requests through the connection mutex.
- Dedicated session-thread launch failures reject and close the new connection rather than escaping the accept loop. Registry allocation happens before starting a joinable thread. Rejection logging cannot throw out of the failure handler.
- No primary-lane routing, table/record lock contract, reactor scheduling or live-server settings change. Application logging changes are not included.

## Local validation

GCC Debug, SQLite enabled, HTTP disabled: 14 focused cases / 294 assertions passed. The delayed-RPC test checks all six file APIs; concurrent close, double close, disconnected handles, in-flight write/disconnect and injected thread-launch failure are covered.

The delayed-RPC regression failed 12 registry-mutex checks on unchanged mtfix39 and passed all 74 assertions on this candidate.

The local selected full sweep actually executed 1,710 cases. One existing flaky exclusive-index test returned 6106 instead of 7040; the same failure reproduced on unchanged mtfix39. 24 cases were excluded/skipped. This is not a claim that all local cases passed. Cross-platform CI is a separate release gate.

The thread-launch regression injects a constructor failure; it does not force real OS thread exhaustion. Reactor-pool startup exception handling is unchanged.

## Application test gate

Repeat B_BIG with the intended remote file logging path and compare time to first browse. Confirm file output, same-handle close/write behavior and clean disconnect. Scale the client storm gradually in a non-production environment. When the dedicated-thread ceiling is reached, new sessions should be rejected and the server should remain responsive. Reactor-pool use still needs separate integrity and capacity testing before production use.
