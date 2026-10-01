# ACE export translation-unit split plan

`src/abi/ace_exports.cpp` contains the public C ABI and tightly coupled process state. Splitting it is a refactor, not a behavior change. The work must preserve exported names, ordinals, calling conventions, handle lifetime, last-error behavior, and recursive-lock re-entry.

## Boundary introduced in this release

Shared process state now lives in `abi/runtime.{h,cpp}` under `openads::abi::detail`. There is still exactly one recursive mutex, handle registry, connection map, and remote-find registry. This small extraction creates a stable internal seam without moving public entry points.

## Planned modules

1. `backend_dispatch` and `remote_state`
2. low-coupling export groups: management, filesystem, compatibility, and stubs
3. connection and teardown paths
4. table, field, index, and transaction exports
5. dictionary and access-control exports
6. SQL statement state, catalogs, procedures, scripts, triggers, and execution

SQL moves last because execution deliberately re-enters public SQL entry points while the recursive ABI lock is held.

## Rules for each move

- Keep C signatures and `ENTRYPOINT` declarations byte-for-byte compatible.
- Do not combine relocation with behavior changes or cleanup.
- Keep one definition of every registry, mutex, and thread-local.
- Preserve `.def` manifests and x86 stdcall aliases.
- Compare produced binary exports against the release baseline in CI.
- Run Windows MSVC and MinGW plus Linux and macOS checks for every move.
- Remove `/bigobj` and `-Wa,-mbig-obj` only after the remaining translation units no longer need them.

Prefer reviewable moves of roughly 500-1,500 lines. Access-control semantics and known parity gaps are separate feature work and must not be hidden inside this refactor.
