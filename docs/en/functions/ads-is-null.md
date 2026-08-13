---
title: AdsIsNull
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-is-null/
---

# AdsIsNull

Tests whether a field in the current record is NULL.

## Syntax

```c
UNSIGNED32 AdsIsNull(ADSHANDLE hTable, UNSIGNED8 *pucField,
                      UNSIGNED16 *pbNull);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle of the table (local, remote, or backend). |
| `pucField` | `UNSIGNED8*` | Field name (NUL-terminated string) or 1-based ordinal via `ADSFIELD(n)`. |
| `pbNull` | `UNSIGNED16*` | Output — `1` if the field is NULL, `0` otherwise. |

## Return Value

`AE_SUCCESS` (0) on success. Non-zero error code on failure
(invalid handle, field not found).

## Description

`AdsIsNull` inspects the NULL bitmap of the current record to
determine whether the specified field is NULL. The NULL bitmap is
only present in ADT tables; for CDX/NTX tables it is always
absent, so the function always reports "not null" (0).

For remote and backend tables the function conservatively
reports "not null" since NULLability isn't exposed over the
wire protocol yet.

## Example

```c
ADSHANDLE hTable;
UNSIGNED16 isNull = 0;
AdsOpenTable(&hTable, "customers.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGotoTop(hTable);
AdsIsNull(hTable, (UNSIGNED8*)"email", &isNull);
if (isNull)
    printf("email is NULL\n");
else
    printf("email has a value\n");
AdsCloseTable(hTable);
```

## See Also

- [AdsGetField]({{ site.baseurl }}/en/functions/ads-get-field/)
- [AdsSetNull]({{ site.baseurl }}/en/functions/ads-set-null/)
- [AdsSetEmpty]({{ site.baseurl }}/en/functions/ads-set-empty/)

---

[← AdsIsIndexUnique]({{ site.baseurl }}/en/functions/ads-is-index-unique/)
[AdsIsRecordInAOF →]({{ site.baseurl }}/en/functions/ads-is-record-in-aof/)
