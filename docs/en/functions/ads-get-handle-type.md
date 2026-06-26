---
title: AdsGetHandleType
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-handle-type/
---

# AdsGetHandleType

Returns the type of an ADS handle (table, connection, statement, etc.).

## Syntax

```c
UNSIGNED32 AdsGetHandleType(ADSHANDLE h, UNSIGNED16 *pusType);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `h` | `ADSHANDLE` | Any valid ADS handle. |
| `pusType` | `UNSIGNED16*` | Output — handle type constant. |

## Return Value

`AE_SUCCESS` (0) on success.

## Handle Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ADS_NONE` | 0 | Unknown / invalid handle. |
| `ADS_TABLE` | 1 | Table handle (local, remote, or backend). |
| `ADS_STATEMENT` | 2 | SQL statement handle. |
| `ADS_CURSOR` | 4 | SQL cursor handle. |
| `ADS_DATABASE_CONNECTION` | 6 | Database connection handle. |
| `ADS_SYS_ADMIN_CONNECTION` | 7 | System admin connection. |

## Description

`AdsGetHandleType` queries the handle registry's `kind_of()`
method to determine the type of any ADS handle. It correctly
distinguishes between tables, connections, statements, and
indexes across all backends (local, remote, SQLite, PostgreSQL,
MariaDB, MSSQL, ODBC, Firebird).

This replaces the previous stub that always returned `ADS_TABLE`.

## Example

```c
ADSHANDLE h;
UNSIGNED16 hType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &h);
AdsGetHandleType(h, &hType);
if (hType == ADS_DATABASE_CONNECTION)
    printf("Handle is a connection\n");
AdsDisconnect(h);
```

## See Also

- [AdsGetConnectionType]({{ site.baseurl }}/en/functions/ads-get-connection-type/)
- [AdsGetTableType]({{ site.baseurl }}/en/functions/ads-get-table-type/)
- [AdsConnect60]({{ site.baseurl }}/en/functions/ads-connect60/)

---

[← AdsGetFilter]({{ site.baseurl }}/en/functions/ads-get-filter/)
[AdsGetIndexCondition →]({{ site.baseurl }}/en/functions/ads-get-index-condition/)
