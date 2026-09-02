# Release notes — v1.09.20 (2026-09-02)

## Fixed — stale wire index id survives silent index overwrite (Pritpal Bedi)

Second `B_BIG.exe` × 700 storm stalled at 16 998 / 70 000 rows and
logged 200 `SetOrder` 5000s ("index not bound to table"). Closing the
wire trace exposed the full mechanism:

1. rddads `adsGoTop` passes `pArea->hOrdCurrent` when non-zero, so
   ordered navigation re-activates the index over the wire (`SetOrder`
   op 140) on every GotoTop.
2. The server-side `AdsCreateIndex61` **silent overwrite** (re-INDEX on
   an existing tag) erased the old ABI binding and minted a **fresh**
   index handle. The remote session's `index_h_` kept pointing at the
   erased one, so the next `SetOrder(iid)` failed with 5000 — and
   `remote_activate_index` kept retrying on every subsequent nav op
   (`server_order_id` stays unknown after a failed SetOrder), which is
   why the failure multiplied.
3. `AdsOpenIndex` already kept old tag→handle mappings on a bag reopen
   (RCB 01/08/2026) for exactly this reason; the create path was the
   gap.

**The fix is two layers:**

- **ABI:** `AdsCreateIndex61`'s silent overwrite now reuses the dropped
  binding's handle instead of minting a fresh one — the same contract
  the reopen refresh already honors. No stale id is created in the
  first place.
- **Wire session (self-heal):** the session remembers each index id's
  tag name (`index_tag_` — populated by OpenIndex / CreateIndex / CDX
  fallback, purged on CloseIndex / CloseTable / disconnect). If a
  `SetOrder` fails with a stale handle, the handler re-resolves the
  tag's *current* binding via `AdsGetIndexHandle` and retries once,
  refreshing `index_h_` on success. This covers every staleness
  source — silent overwrite, `AdsCloseAllIndexes` purge, twin
  replacement — not just the create path.

This ships with the earlier 1.09.20-rc1 B_BIG fixes (OpenIndex header
race 5103 → 5018/7040, `SetOrder` iid-0 natural order, blocking append
record lock, append queue with ACE lock-budget timeout).

## Upgrade notes

- Drop-in replacement for v1.09.x serverd and ace32/ace64; no config
  or client changes.
- Remote clients holding index handles across `INDEX ON` on an
  existing tag (silent overwrite) no longer lose the order — ordered
  browses keep working instead of falling back to natural order.

## What to test (Pritpal)

1. Re-run B_BIG with 700 instances against this build
   (`max_sessions = 1000` in `openads.ini` as before).
2. `ads_err` must show no `SetOrder` 5000s; the ordered
   `Browse()` storm should progress without losing the index.
3. Final record count must still be exactly 100 × instances.