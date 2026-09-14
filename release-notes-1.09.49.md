# OpenADS v1.09.49

## Mutex leak fix: dead sessions release their names

Field (v1.09.48, loopback and WAN): `OAds_MutexCreate(cMutexName)`
failed after the creating process died — only a server restart
cleared it, blocking extensive multi-user testing. Root cause:
`Session::cleanup` released indexes, tables, files and ABI handles
but never touched the distributed mutex service
(`MutexManager::release_all` existed but was dead code — and it
only unlocked without destroying, so the name wedged forever).

### Changes

- **Creator tracking** (`src/network/mutex_manager.*`):
  `create` records the creating session; new `release_session`
  unlocks everything the dying session held and destroys every
  name it created — unless a different live session currently
  holds the lock, in which case creation transfers to that
  holder instead of destroying under it. Waiters are woken.
- **Teardown hook** (`src/network/session.cpp`): `Session::cleanup`
  runs `release_session` first, covering Disconnect, peer-close
  (killed process) and exception paths alike — all funnel through
  it. No protocol change.
- **Tests:** new `network_mutex_release_test` — creator dies
  holding the lock (recreate+lock must succeed), and creator dies
  while a peer holds it (peer keeps working; name dies with the
  peer). Verified the test fails without the fix exactly as the
  field reported (`MutexCreate` → error after creator death).

### Test Results

- MinGW-x64 (GCC 16.2): mutex + pool + batching suites green;
  full unit suite 1569/1570 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.49-windows-x64.zip`
- `openads-1.09.49-windows-x86.zip`
- `openads-1.09.49-linux-x64.tar.gz`
- `openads-1.09.49-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
