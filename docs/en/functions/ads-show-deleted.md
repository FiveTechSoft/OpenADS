---
title: AdsShowDeleted
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-show-deleted/
---

# AdsShowDeleted

Toggles whether deleted records are visible (SET DELETED ON/OFF).

## Syntax

```c
UNSIGNED32 AdsShowDeleted(UNSIGNED16 us);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `us` | `UNSIGNED16` | `1` = show deleted records (SET DELETED OFF, Clipper default); `0` = hide deleted records (SET DELETED ON). |

## Return Value

`AE_SUCCESS` (0) always.

## Description

`AdsShowDeleted` controls whether navigation (`GotoTop`, `Skip`, index
walks) and filter-aware counts skip rows flagged deleted in the DBF.

Locally, the flag is process-wide and also stored on the active
`Connection`. Over a **remote** `tcp://` connection (since v1.8.10 /
M12.31), the client forwards the setting to every open server session
via the `ShowDeleted` wire opcode (`0xDA`). The server applies it to
its session connection and to the ABI connection used for ordered /
scoped navigation, so `OrdScope` walks honour SET DELETED ON the same
way as local mode.

Since v1.8.11 (M12.32) the call order no longer matters: a connection
opened **after** `AdsShowDeleted(0)` syncs the state right after the
connect handshake, so the usual rddads / FiveWin startup (`SET DELETED
ON` in `Main`, then `AdsConnect60`) hides deleted rows remotely too.
The server also re-applies the flag to the ABI connection it creates
lazily for ordered/scoped navigation.

Upgrade **both** `openace64.dll` and `openads_serverd` together; older
servers ignore the opcode (best-effort) and continue to return deleted
rows inside a scoped remote walk.

## Example

```c
AdsShowDeleted(0);  // SET DELETED ON — hide deleted rows
AdsGotoTop(hOrd);
while (1) {
    UNSIGNED16 eof = 0;
    AdsAtEOF(hTable, &eof);
    if (eof) break;
    /* ... process live row ... */
    AdsSkip(hOrd, 1);
}
AdsShowDeleted(1);  // restore default (show deleted)
```

## See Also

- [AdsGetDeleted]({{ site.baseurl }}/en/functions/ads-get-deleted/)
- [AdsDeleteRecord]({{ site.baseurl }}/en/functions/ads-delete-record/)
- [AdsIsRecordDeleted]({{ site.baseurl }}/en/functions/ads-is-record-deleted/)
- [Wire protocol — ShowDeleted]({{ site.baseurl }}/en/wire-protocol/#526-showdeleted-m1231)

---

[← AdsGetRecordCount]({{ site.baseurl }}/en/functions/ads-get-record-count/)