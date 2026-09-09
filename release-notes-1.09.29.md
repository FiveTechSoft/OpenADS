# OpenADS v1.09.29

## Nav-probe batching: ~80% off WAN round-trips per USE (Vouch 197s)

Field trace (`cli_trace.log`, 199 lines) proved each table USE pays
~33 wire round-trips on bare navigation probing — `GotoTop×2`,
`AtBOF/AtEOF` pairs, `GotoBottom` — before counting open/order costs.
At ~100 ms RTT that is ~2 s per USE × ~90 tables ≈ 197 s of startup.
Three kills, all client-transparent, all proven by opcode counters:

### Changes

- **Duplicate GotoTop/GotoBottom suppression.** Back-to-back identical
  navs with nothing on the wire since skip their frame (keyno sync +
  relation apply still re-run locally). The stamp is order-aware
  (index id / ack-confirmed server binding): a top in order A never
  suppresses order B.
- **Empty-cursor sticky.** A top/bottom yielding no row proves an
  empty cursor — simultaneously BOF and EOF — so all further boundary
  probes answer locally until anything touches the wire. Purely-local
  visibility mutations (`AdsSetFilter`/`ClearFilter`) expire the stamp
  explicitly; prefetch drains reset it (local move, no frame).
- **BOF/EOF twin flag.** `AtBOFAck` is now `[u8 bof][u8 eof]` and
  `AtEOFAck` is `[u8 eof][u8 bof]`; the client caches the twin half,
  so rddads' inevitable pair costs one round-trip. Length-gated
  trailing byte: old servers send one byte, old clients read one —
  every version mix behaves as before, no fallback paths. Also fixed
  a latent stale twin after Limbo-rescue found along the way.
- **Tests.** New `tests/unit/network_nav_batch_test` (4 cases:
  duplicate 0 frames, empty table 0 frames, twin pair 1 frame,
  order-context). Full suite green except the pre-existing
  MinGW-only CDX alloc-tail case.
- **Docs.** Wire spec §5.10 documents the twin-byte acks.

### Compatibility

- No new opcodes, no changed request layouts. Mixed old/new peers
  behave exactly as before (twin caching engages only when the ack
  carries the second byte).

### Test Results

- MinGW-x64 (GCC 16.2): batching suite 4/4; full unit suite
  1539/1540 (sole failure pre-existing, MinGW-only, fails
  identically without these changes).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.29-windows-x64.zip`
- `openads-1.09.29-windows-x86.zip`
- `openads-1.09.29-linux-x64.tar.gz`
- `openads-1.09.29-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
