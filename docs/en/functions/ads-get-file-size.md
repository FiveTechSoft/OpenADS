---
title: AdsGetFileSize
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-file-size/
---

# AdsGetFileSize

Returns the size in bytes of a file under the data directory.

Harbour-style name: `oads_FSize` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsGetFileSize(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                          UNSIGNED32 *pulSize);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative file path. |
| `pulSize` | `UNSIGNED32*` | Out: file size in bytes (v1 capped at 4 GiB−1). |

## Return Value

`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if not a regular file. `AE_INTERNAL_ERROR` if size exceeds `UNSIGNED32`.

## Description

For remote connections the size is read on the server.

## Example

```c
UNSIGNED32 sz = 0;
AdsGetFileSize(hConn, (UNSIGNED8*)"inbox/hello.txt", &sz);
```

## See Also

- [AdsGetFileTime]({{ site.baseurl }}/en/functions/ads-get-file-time/)
- [AdsGetFileDate]({{ site.baseurl }}/en/functions/ads-get-file-date/)
- [AdsCheckExistence]({{ site.baseurl }}/en/functions/ads-check-existence/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
