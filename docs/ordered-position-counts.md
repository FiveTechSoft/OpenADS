# Remote ordered-position count traffic

Remote AdsGetRelKeyPos now asks for a physical record count only in natural
order. With an active index, it uses the existing scoped key count and skips
the physical value that it would otherwise discard.

The nested remote position jump uses the same rule. AdsSetRelKeyPos keeps its
existing physical-count target calculation and passes that snapshot to the
natural-order jump instead of asking the helper to ensure it again. Under the
current cached-count policy, that second ensure normally already costs no RPC.
This port does not promise an additional wire reduction for that setter today.
An ordered jump still uses the scoped key count to clamp its target.

Pending buffered writes are still flushed before a jump. Index activation,
scope handling, count/key caches, natural-order movement and error checks keep
their existing behavior. No protocol, Harbour RDD core, record-lock contract,
count freshness policy or OPENADS_FRESH_COUNTS option is changed.

Tests explicitly clear only the physical-count cache to measure the discarded
RPC. They check table and index handles, a narrower numeric scope, preserved
setter targets and natural-order get/set behavior. Clearing this cache in the
test is not a production freshness option.
