---
title: AdsGetKeyLength
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-key-length/
---

# AdsGetKeyLength

Returns the length in bytes of the index key for the active order.

## Syntax

```c
UNSIGNED32 AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16 *pusKeyLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle of the index order. |
| `pusKeyLen` | `UNSIGNED16*` | Output — key length in bytes. |

## Return Value

`AE_SUCCESS` (0) on success. Non-zero error code if the handle
doesn't resolve to an active index.

## Description

`AdsGetKeyLength` returns the width of a single key entry in the
active B+tree index. The key length is determined at index
creation time from the expression and field types. For character
keys this is typically the sum of field widths; for numeric/date
keys it is 8 bytes (FoxNumeric encoding).

## Example

```c
ADSHANDLE hIndex;
UNSIGNED16 keyLen = 0;
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetKeyLength(hIndex, &keyLen);
printf("Key length: %u bytes\n", keyLen);
```

## See Also

- [AdsGetKeyType]({{ site.baseurl }}/en/functions/ads-get-key-type/)
- [AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/en/functions/ads-extract-key/)

---

[← AdsGetKeyNum]({{ site.baseurl }}/en/functions/ads-get-key-num/)
[AdsGetKeyType →]({{ site.baseurl }}/en/functions/ads-get-key-type/)
