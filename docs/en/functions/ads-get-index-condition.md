---
title: AdsGetIndexCondition
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-index-condition/
---

# AdsGetIndexCondition

Returns the FOR condition expression of an index order.

## Syntax

```c
UNSIGNED32 AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8 *pucBuf,
                                UNSIGNED16 *pusBufLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle of the index order. |
| `pucBuf` | `UNSIGNED8*` | Output buffer for the condition string. |
| `pusBufLen` | `UNSIGNED16*` | Input/output — buffer capacity on entry, actual length on return. |

## Return Value

`AE_SUCCESS` (0) on success. Returns an empty string if the
index has no FOR condition.

## Description

`AdsGetIndexCondition` retrieves the conditional (FOR) expression
associated with an index tag. Not all indexes have a FOR
condition; when absent, the function returns an empty string with
`*pusBufLen = 0`.

This function follows the same handle-resolution pattern as
[AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/):
it checks the parked binding first, then the active order's IIndex.

## Example

```c
ADSHANDLE hIndex;
char cond[256];
UNSIGNED16 len = sizeof(cond);
AdsGetIndexHandle(hTable, "active_cust", &hIndex);
AdsGetIndexCondition(hIndex, (UNSIGNED8*)cond, &len);
if (len > 0)
    printf("FOR condition: %s\n", cond);
else
    printf("No FOR condition\n");
```

## See Also

- [AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/)
- [AdsGetIndexFilename]({{ site.baseurl }}/en/functions/ads-get-index-filename/)
- [AdsOpenIndex]({{ site.baseurl }}/en/functions/ads-open-index/)

---

[← AdsGetHandleType]({{ site.baseurl }}/en/functions/ads-get-handle-type/)
[AdsGetIndexFilename →]({{ site.baseurl }}/en/functions/ads-get-index-filename/)
