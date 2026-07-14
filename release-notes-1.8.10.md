## Changes

### REMOTE — SET DELETED ON on scoped index walks (M12.31)

- **`AdsShowDeleted(0)` now reaches the server** over wire opcode `ShowDeleted` (0xDA/0xDB). Scoped remote walks (`OrdScope` / `AdsSetScope` + `GotoTop`/`Skip`) skip deleted rows like local mode.
- **`SetScope` server handler** also syncs the ABI table's active order via `AdsSetIndexOrderByHandle`.
- **Deadlock fix**: client releases `s.mu` before the `ShowDeleted` wire round-trip (in-process server / test harness).

Requires updated **openace64.dll** and **openads_serverd** on the same build.

Full details in [CHANGELOG.md](CHANGELOG.md).