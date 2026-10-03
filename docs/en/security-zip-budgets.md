# Remote archive resource limits

Remote ZIP creation, extraction and listing enforce a per-engine-call budget:
100,000 entries, 1 GiB of source or declared expanded data, 64 MiB of names and
comments (list/extraction accounting includes 64 bytes per entry), and a
cooperative 30-second deadline. Local callers keep unrestricted defaults.
ZIP creation checks all staged sizes before deleting an existing archive and
rejects sources that grow beyond their staged sizes while streaming. Extraction
checks declared sizes before opening an output and rejects actual output beyond
the declared entry size. Listings stop before accumulating over-budget entries.

The deadline is checked between entries and streaming chunks, not a hard timeout
for blocking filesystem calls. Connection-level preflight and extraction use
separate engine calls and therefore separate deadlines. Previously extracted
files can remain if a later entry exceeds a budget. General failed-overwrite
preservation and concurrent symlink replacement are separate audit items.
These limits reduce per-request archive resource abuse; they do not imply the
security audit is complete or aggregate disk-use quotas are enforced.
