---
title: AdsCacheRecords
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-cache-records/
---

# AdsCacheRecords

Hints how many records to read ahead for a table.

## Syntax

```c
UNSIGNED32 AdsCacheRecords(ADSHANDLE hTable, UNSIGNED16 usNumRecords);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle of the table. |
| `usNumRecords` | `UNSIGNED16` | Suggested number of records to read ahead. |

## Return Value

`AE_SUCCESS` (0) on success. `AE_INTERNAL_ERROR` (5000) for an unknown handle.

## Example

```c
/* Batch update sweep: we edit nearly every record we visit, so every
   write would dump the read-ahead block. Don't read ahead at all. */
AdsCacheRecords(hTable, 0);
for (AdsGotoTop(hTable); !at_eof(hTable); AdsSkip(hTable, 1)) {
    AdsSetString(hTable, field, val, len);
    AdsWriteRecord(hTable);
}

/* Read-only sweep in one direction: pull deep blocks. */
AdsCacheRecords(hTable, 100);
```

## Description

Sets how many records a forward `AdsSkip` reads ahead into the client cache on a remote connection.

**You usually do not need to call this.** By default OpenADS chooses the depth itself: a forward `AdsSkip` returns a block of look-ahead rows alongside the current one, and the client serves subsequent skips from that block with no round-trip. The server ramps the depth up as it detects a sequential scan and drops it back on any reposition, write, or order change — so a browse gets a deep block, while a one-off "seek a record and read it" does not pay for rows it will never look at. (SAP's default is a fixed ten records; OpenADS adapts instead.)

Call `AdsCacheRecords` when you know something about your access pattern that the server cannot infer from it:

| `usNumRecords` | Effect |
|-----------|--------|
| `0` or `1` | **Turns read-ahead off** for this table. |
| `N` | Read ahead exactly `N` records per skip, overriding the automatic ramp. |

Turning caching **off** is the right call for a batch loop that edits most of the records it visits: every write drops the cached block anyway, so reading ahead is pure waste. Turning it **up** (SAP's "aggressive" setting is 100) suits a one-directional sweep that reads a large portion of a table without editing. SAP notes that values above 100 are generally not beneficial; OpenADS caps a single block at 512 records regardless, so one request cannot turn into an unbounded server-side scan.

The depth is a ceiling, not a promise: a block is also bounded by a byte budget, so a table with wide records returns fewer rows than requested rather than an oversized network frame.

Read-ahead never serves stale data of your own making — the block is discarded on any write, seek, order change, or `AdsRefreshRecord`. As with SAP, rows already in the block do **not** reflect changes made concurrently by *other* users; call [AdsRefreshRecord]({{ site.baseurl }}/en/functions/ads-refresh-record/) to force a re-read.

On a local (in-process) connection there is no network to read ahead of, and the driver already keeps a block cache under the cursor; the call validates the handle and succeeds.

## See Also

- [AdsCacheOpenTables]({{ site.baseurl }}/en/functions/ads-cache-open-tables/)
- [AdsCacheOpenCursors]({{ site.baseurl }}/en/functions/ads-cache-open-cursors/)

---

[AdsCacheOpenTables →]({{ site.baseurl }}/en/functions/ads-cache-open-tables/)
