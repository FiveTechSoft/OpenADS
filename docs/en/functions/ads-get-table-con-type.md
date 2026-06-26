---
title: AdsGetTableConType
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-table-con-type/
---

# AdsGetTableConType

Returns the type of table (CDX, NTX, ADT, etc.).

## Syntax

```c
UNSIGNED32 AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16 *pusType);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle of the table. |
| `pusType` | `UNSIGNED16*` | Output — table type constant. |

## Return Value

`AE_SUCCESS` (0) on success.

## Table Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ADS_CDX` | 1 | CDX compound index (FoxPro/Harbour). |
| `ADS_NTX` | 2 | NTX index (Clipper). |
| `ADS_ADT` | 5 | ADT table (Advantage native). |

## Description

`AdsGetTableConType` delegates to `AdsGetTableType` which
derives the table type from the file extension (`.dbf` → CDX,
`.adt` → ADT). This replaces the previous stub that always
returned `ADS_CDX`.

## Example

```c
ADSHANDLE hTable;
UNSIGNED16 tableType = 0;
AdsOpenTable(&hTable, "data.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGetTableConType(hTable, &tableType);
if (tableType == ADS_ADT)
    printf("ADT table\n");
else
    printf("DBF/CDX table\n");
AdsCloseTable(hTable);
```

## See Also

- [AdsGetTableType]({{ site.baseurl }}/en/functions/ads-get-table-type/)
- [AdsOpenTable]({{ site.baseurl }}/en/functions/ads-open-table/)
- [AdsCreateTable]({{ site.baseurl }}/en/functions/ads-create-table/)

---

[← AdsGetTableCharType]({{ site.baseurl }}/en/functions/ads-get-table-char-type/)
[AdsGetTableConnection →]({{ site.baseurl }}/en/functions/ads-get-table-connection/)
