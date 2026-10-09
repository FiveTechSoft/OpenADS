# Physical record-count refresh

For ordinary DBFs, a physical count refresh derives the row count from the
file body length, matching Harbour DBFCDX's file-size rule. A stale high
header no longer resurrects rows after a peer shrinks or empties the body;
a stale low header does not hide complete rows already on disk. Optional
EOF bytes are excluded by integer division for ordinary record lengths.

Full-table encryption retains fixed-length records. Partial encryption has
a trailing bitmap and keeps its existing header-count path instead. This
patch does not change either encryption format.

The driver refresh result now reaches local `AdsGetRecordCount` and the
server's `GetRecordCount` reply. A failed read/size query or truncated DBF
returns an error, not a successful stale count. Local caller output is not
assigned on refresh failure. NTX, ADT and cache wrappers forward the result.
Existing navigation refresh calls are not broadened into a new error policy.

No remote count-cache policy, `OPENADS_FRESH_COUNTS` switch, ordered-position
optimization, protocol change or Harbour RDD-core edit is included. A client
cache hit still follows the existing policy and may miss a peer append;
this patch corrects the value and error result when a physical refresh occurs.

This work is carried in Pritpal Bedi's mtfix41 fork release. The upstream
regressions cover stale headers, peer append/shrink, empty/EOF-only bodies,
truncation errors on local and direct-wire paths, and encrypted counts.
