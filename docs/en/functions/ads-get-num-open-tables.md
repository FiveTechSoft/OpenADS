---
title: AdsGetNumOpenTables
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-num-open-tables/
---

# AdsGetNumOpenTables

Returns the number of tables currently open in the process.

## Syntax

```c
UNSIGNED32 AdsGetNumOpenTables(UNSIGNED16 *pusNumTables);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pusNumTables` | `UNSIGNED16*` | Output — count of open tables. |

## Return Value

`AE_SUCCESS` (0) on success.

## Description

`AdsGetNumOpenTables` iterates the handle registry and counts
all registered table handles across all backends: local tables,
remote tables, SQLite, PostgreSQL, MariaDB, MSSQL, ODBC, and
Firebird tables.

This replaces the previous stub that always returned 0.

## Example

```c
UNSIGNED16 numTables = 0;
AdsOpenTable(&hTable1, "customers.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsOpenTable(&hTable2, "orders.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsGetNumOpenTables(&numTables);
printf("Open tables: %u\n", numTables);  // 2
```

## See Also

- [AdsOpenTable]({{ site.baseurl }}/en/functions/ads-open-table/)
- [AdsCloseAllTables]({{ site.baseurl }}/en/functions/ads-close-all-tables/)
- [AdsGetNumOpenIndexes]({{ site.baseurl }}/en/functions/ads-get-num-indexes/)

---

[← AdsGetNumLocks]({{ site.baseurl }}/en/functions/ads-get-num-locks/)
[AdsGetRecord →]({{ site.baseurl }}/en/functions/ads-get-record/)
