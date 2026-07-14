---
title: AdsGetDeleted
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-deleted/
---

# AdsGetDeleted

Returns whether deleted records are visible (SET DELETED status).

## Syntax

```c
UNSIGNED32 AdsGetDeleted(UNSIGNED16 *pusDeleted);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pusDeleted` | `UNSIGNED16*` | Output — 1 if deleted records are visible (SET DELETED OFF), 0 if hidden (SET DELETED ON). |

## Return Value

`AE_SUCCESS` (0) on success.

## Description

`AdsGetDeleted` returns the current state of the `SET DELETED` flag, which controls whether deleted records are visible to database operations.

- Returns **1** when `show_deleted()` is true — deleted records ARE visible (SET DELETED OFF, the Clipper default)
- Returns **0** when `show_deleted()` is false — deleted records are hidden (SET DELETED ON)

This matches the ACE semantics where `AdsGetDeleted` returns 1 when deleted records are visible.

In OpenADS, this setting is process-wide and affects all tables. The default behavior is SET DELETED OFF (deleted records visible), which is the Clipper default.

`AdsGetDeleted` reads the **client** state only. On remote connections,
call `AdsShowDeleted` to push SET DELETED ON/OFF to the server (v1.8.10+);
there is no separate wire query for the server's copy of the flag.

## Example

```c
UNSIGNED16 usDeleted;

AdsGetDeleted(&usDeleted);
if (usDeleted)
    printf("Deleted records are visible (SET DELETED OFF)\n");
else
    printf("Deleted records are hidden (SET DELETED ON)\n");

// Toggle the setting
AdsShowDeleted(!usDeleted);

AdsGetDeleted(&usDeleted);
printf("After toggle: %s\n", usDeleted ? "visible" : "hidden");
```

## See Also

- [AdsShowDeleted]({{ site.baseurl }}/en/functions/ads-show-deleted/)
- [AdsDeleteRecord]({{ site.baseurl }}/en/functions/ads-delete-record/)
- [AdsRecallRecord]({{ site.baseurl }}/en/functions/ads-recall-record/)

---

[← AdsGetAOF]({{ site.baseurl }}/en/functions/ads-get-aof/)
[AdsGetEpoch →]({{ site.baseurl }}/en/functions/ads-get-epoch/)
