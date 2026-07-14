## Changes

### REMOTE — SET DELETED ON issued before connect (M12.32)

- **`SET DELETED ON` before `AdsConnect60` now reaches the server** — the startup order every rddads / FiveWin app uses. The client syncs the `ShowDeleted` state right after the connect handshake; v1.8.10 only notified connections already open when `AdsShowDeleted` was called.
- **Server re-applies the flag to the lazy ABI connection** it creates at the first `SetScope`/`SetOrder`, so ordered/scoped walks (`OrdScope` + `GotoTop`/`Skip`) skip deleted rows regardless of call order.
- New wire probe `tools/remote_deleted_probe.cpp` verifies the flow end-to-end against a live `openads_serverd`.

Requires updated **openace64.dll** and **openads_serverd** on the same build.

Full details in [CHANGELOG.md](CHANGELOG.md).
