---
title: AdsGetFileDate
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-file-date/
---

# AdsGetFileDate

Returns the last-modified date of a file as `"YYYYMMDD"`.

Harbour-style name: `oads_FDate` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsGetFileDate(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                          UNSIGNED8 *pucDate, UNSIGNED16 *pusLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative file path. |
| `pucDate` | `UNSIGNED8*` | Buffer for the date string. |
| `pusLen` | `UNSIGNED16*` | In/out length; need at least 9. |

## Return Value

`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` if too small.

## Description

Pair with `AdsGetFileTime` for full mtime. Harbour wrappers can convert `"YYYYMMDD"` to a date value.

## Example

```c
UNSIGNED8 d[16]; UNSIGNED16 n = sizeof(d);
AdsGetFileDate(hConn, (UNSIGNED8*)"x.txt", d, &n);
```

## See Also

- [AdsGetFileTime]({{ site.baseurl }}/en/functions/ads-get-file-time/)
- [AdsGetFileSize]({{ site.baseurl }}/en/functions/ads-get-file-size/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
