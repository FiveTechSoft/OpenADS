# CDX bottom navigation

`DbGoBottom()` / `AdsGotoBottom()` on a CDX order now reaches the rightmost
B-tree leaf directly instead of walking every leaf from the left. The index
header is refreshed first so a changed peer root is not ignored. Empty leaves
at the right edge are skipped toward the last live key.

The change retains the existing key/record-number ordering, duplicate-key
behavior and index cursor interface. An empty or fully erased tree still has
no positioned key. A malformed branch that exceeds the page or reaches the
64-level traversal limit returns an error rather than walking indefinitely.

This is the CDX navigation work developed from Pritpal Bedi's B_BIG trace and
carried in the bedipritpal/OpenADS mtfix41 release. It does not change record
locks, physical record-count policy, the Harbour RDD core, the wire protocol,
network security settings or build/release workflows.

Regression coverage includes a multilevel tree with 20,000 duplicate keys,
a peer append after a reader has opened the index, backward movement from the
last key, erased rightmost leaves, an empty index and a fully erased tree.
No quantified latency or page-read improvement is claimed by these tests.
