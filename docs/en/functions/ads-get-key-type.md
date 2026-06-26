---
title: AdsGetKeyType
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-key-type/
---

# AdsGetKeyType

Returns the encoding type of the index key for the active order.

## Syntax

```c
UNSIGNED32 AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16 *pusKeyType);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle of the index order. |
| `pusKeyType` | `UNSIGNED16*` | Output — key type constant. |

## Return Value

`AE_SUCCESS` (0) on success.

## Key Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ADS_RAWKEY` | 0 | Raw binary key bytes. |
| `ADS_STRINGKEY` | 1 | Character / space-padded string key. |
| `ADS_DOUBLEKEY` | 2 | Numeric or date key (FoxNumeric / NtxNumeric 8-byte encoding). |

## Description

`AdsGetKeyType` inspects the `KeyEncoding` of the active index
and maps it to the ACE key type constants. Character expression
indexes return `ADS_STRINGKEY`; numeric and date expression
indexes return `ADS_DOUBLEKEY`.

## Example

```c
ADSHANDLE hIndex;
UNSIGNED16 keyType = 0;
AdsGetIndexHandle(hTable, "amount", &hIndex);
AdsGetKeyType(hIndex, &keyType);
if (keyType == ADS_DOUBLEKEY)
    printf("Numeric index key\n");
else
    printf("Character index key\n");
```

## See Also

- [AdsGetKeyLength]({{ site.baseurl }}/en/functions/ads-get-key-length/)
- [AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/en/functions/ads-extract-key/)

---

[← AdsGetKeyLength]({{ site.baseurl }}/en/functions/ads-get-key-length/)
[AdsGetLastTableUpdate →]({{ site.baseurl }}/en/functions/ads-get-last-table-update/)
