---
title: AdsGetKeyType
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-key-type/
---

# AdsGetKeyType

Returns the data type of the index key expression.

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
| `ADS_LOGICAL` | 1 | Logical key expression. |
| `ADS_NUMERIC` | 2 | Numeric key expression. |
| `ADS_DATE` | 3 | Date key expression. |
| `ADS_STRING` | 4 | Character key expression. |
| `ADS_RAW` | 16 | Raw key expression (ADT `;` concatenation). |

Note these are the *field-type* constants, not the buffer-encoding
constants (`ADS_STRINGKEY` / `ADS_DOUBLEKEY` / `ADS_RAWKEY`) used by
`AdsSeek` and `AdsSetScope`.

## Description

`AdsGetKeyType` reports the result type of the index key expression.
A bare field key answers from the table schema (a `C` field returns
`ADS_STRING`, an `N`/`F`/`I`/`B`/`Y` field `ADS_NUMERIC`, a `D` field
`ADS_DATE`, an `L` field `ADS_LOGICAL`). Computed expressions report
by their result type: string expressions such as `UPPER(name)` return
`ADS_STRING`; numeric expressions such as `Val(code)` return
`ADS_NUMERIC`.

Language bindings rely on this value to encode scope and seek keys —
Harbour's rddads, for example, chooses the `OrdScope()` key encoding
from it.

## Example

```c
ADSHANDLE hIndex;
UNSIGNED16 keyType = 0;
AdsGetIndexHandle(hTable, "amount", &hIndex);
AdsGetKeyType(hIndex, &keyType);
if (keyType == ADS_NUMERIC)
    printf("Numeric index key\n");
else if (keyType == ADS_STRING)
    printf("Character index key\n");
```

## See Also

- [AdsGetKeyLength]({{ site.baseurl }}/en/functions/ads-get-key-length/)
- [AdsGetIndexExpr]({{ site.baseurl }}/en/functions/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/en/functions/ads-extract-key/)

---

[← AdsGetKeyLength]({{ site.baseurl }}/en/functions/ads-get-key-length/)
[AdsGetNumActiveLinks →]({{ site.baseurl }}/en/functions/ads-get-num-active-links/)
