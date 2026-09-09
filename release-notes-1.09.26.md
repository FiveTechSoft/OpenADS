# OpenADS v1.09.26

## Startup latency: lazy-close pooling + warm opens (RDD-only apps, no app changes)

Vouch-style apps re-`USE` the same lookup tables every few seconds;
each `USE` cost open+index+describe+goto round-trips (~4 RTTs, more
with order/count tails). This release collapses repeats to a single
warm `GotoTop` and first opens to ~2 RTTs.

### Changes

- **Lazy-close table pool.** Closing an eligible table parks its
  server handle, schema, tags and order bindings instead of closing;
  the next `USE` of the same path+alias+mode revalidates with one
  warm `GotoTop`. Eligibility is conservative (shared mode, natural
  order, never locked/scoped, no AOF/filter/relations, short TTL,
  bounded pool, full flush on disconnect). File-lifecycle ops
  (drop/create/erase/rename, exclusive opens) evict first, so
  drop-after-close and create-overwrite behave exactly as before.
  Also fixes a `RemoteTable` leak (nothing ever erased the ownership
  map).
- **Warm `OpenTableAck`.** Schema + first row (with lookahead block)
  ride the open reply as length-gated TLV sections; new clients skip
  `DescribeTable` + implicit `GotoTop`. Old peers fall back
  untouched.
- **Orphan-twin fix (correctness).** A *failed* `OpenIndex` left its
  freshly created server-side ABI twin behind; later writes went to
  the twin while `fetch` packed the engine table (stale blanks until
  a nav reloaded). Failed opens now drop a twin they created.
- **Fixed-width `ads_err.log` text mirror** with
  `PID`/`TID`/`SESSION`/`CLIENT`/`OP`/`TABLE` columns; per-frame
  context from the server dispatch.
- **Tests.** Pooled re-USE (zero `OpenTable`/`CloseTable` frames),
  lock-ineligibility, drop-after-park, warm-open counters; date
  expectations aligned with display behavior; MSVC `/W4` clean.

### Compatibility

- Client-only pooling + additive wire sections: old servers/clients
  behave exactly as before (verified: full suite green on the
  pre-sections paths).

### Test Results

- Windows MSVC x64/x86, Harbour smoke, PHP, SQL smokes: green.
- Linux: green except pre-existing load-sensitive race tests and one
  POSIX-semantics case (`openindex_create_race`: same-process `flock`
  cannot self-conflict, so 7040 is unreachable there by design).

### Packages

- `openads-1.09.26-windows-x64.zip`
- `openads-1.09.26-windows-x86.zip`
- `openads-1.09.26-linux-x64.tar.gz`
- `openads-1.09.26-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
