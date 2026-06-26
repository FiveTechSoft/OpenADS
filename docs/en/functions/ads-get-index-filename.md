---
title: AdsGetIndexFilename
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-index-filename/
---

# AdsGetIndexFilename

Returns the file path of the index file for a given order.

## Syntax

```c
UNSIGNED32 AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 usOrder,
                                UNSIGNED8 *pucBuf, UNSIGNED16 *pusBufLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle of the index order. |
| `usOrder` | `UNSIGNED16` | Reserved, pass 0. |
| `pucBuf` | `UNSIGNED8*` | Output buffer for the file path. |
| `pusBufLen` | `UNSIGNED16*` | Input/output — buffer capacity on entry, actual length on return. |

## Return Value

`AE_SUCCESS` (0) on success.

## Description

`AdsGetIndexFilename` returns the resolved file-system path of
the `.cdx` / `.ntx` / `.adi` file that contains the given index
order. The path is stored in the `IndexBinding` at handle
registration time and is always accurate for both active and
parked indexes.

## Example

```c
ADSHANDLE hIndex;
char path[260];
UNSIGNED16 len = sizeof(path);
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetIndexFilename(hIndex, 0, (UNSIGNED8*)path, &len);
printf("Index file: %s\n", path);
```

## See Also

- [AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/)
- [AdsGetIndexCondition]({{ site.baseurl }}/en/functions/ads-get-index-condition/)
- [AdsOpenIndex]({{ site.baseurl }}/en/functions/ads-open-index/)

---

[← AdsGetIndexCondition]({{ site.baseurl }}/en/functions/ads-get-index-condition/)
[AdsGetIndexOrderByHandle →]({{ site.baseurl }}/en/functions/ads-get-index-order-by-handle/)
