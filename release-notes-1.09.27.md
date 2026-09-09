# OpenADS v1.09.27

## Pool order-active tables (Vouch IndexOrd=1 stays pooled across USEs)

v1.09.26 pooled only natural-order closes. Vouch-style apps keep
`IndexOrd()=1` across `USE`s, so almost every close was order-active
and the pool stayed empty for exactly the workload it was built for.
This release drops the natural-order requirement: order/index bindings
are per-session server-side and no close reaches the server while
parked, so an active order resumes exactly on reuse (one warm `GotoTop`
repositions, same as before).

### Changes

- **Order-active pooling.** `remote_table_poolable()` no longer
  excludes `active_index_id != 0` / `server_order_id != 0`. All other
  eligibility stays conservative (shared mode, never locked/scoped, no
  AOF/filter/relations, short TTL, bounded pool, full flush on
  disconnect; drop/create/erase/rename/exclusive-open evict first).
- **File-lifecycle contract (documented + tested).** A parked close
  keeps server files open until the entry expires or the connection
  drops. On Windows a raw out-of-band delete in that window fails with
  a sharing violation, so file lifecycle must go through the `Ads*`
  entry points (`AdsDropTable` / `AdsDeleteFile` / `AdsRenameFile` /
  `AdsCreateTable`), which evict parked handles first. The zombie
  file-release test now proves this path: ordered close → park →
  `AdsDeleteFile` evicts → both `.DBF` and `.CDX` really gone.
- **Tests.** Zombie lock suite updated (erase-via-`AdsDeleteFile` with
  `EnableFileFunc`, per the fs-ops gate); pooled re-USE, lock-
  ineligibility, drop-after-park, warm-open and `SetFields` suites
  unchanged and green.

### Compatibility

- Client-only change on top of v1.09.26 wire behavior: old
  servers/clients behave exactly as before.

### Test Results

- MinGW-x64 (GCC 16.2): pool/warm/setfields/zombie suites green;
  full unit suite green except one pre-existing MinGW-only failure
  (`CdxIndex create resets page allocator tail`, fails identically
  with and without this change; MSVC CI was green for v1.09.26).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.27-windows-x64.zip`
- `openads-1.09.27-windows-x86.zip`
- `openads-1.09.27-linux-x64.tar.gz`
- `openads-1.09.27-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
