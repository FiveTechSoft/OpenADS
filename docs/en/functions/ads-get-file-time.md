---
title: AdsGetFileTime
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-file-time/
---

# AdsGetFileTime

Returns the last-modified time of a file as `"hh:mm:ss"` (local server clock).

Harbour-style name: `oads_FTime` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsGetFileTime(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                          UNSIGNED8 *pucTime, UNSIGNED16 *pusLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative file path. |
| `pucTime` | `UNSIGNED8*` | Buffer for the time string (null-terminated). |
| `pusLen` | `UNSIGNED16*` | In: buffer capacity. Out: length written (including NUL). Need at least 9. |

## Return Value

`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` (5051) if the buffer is too small (`*pusLen` set to required size).

## Description

Uses the server (or local process) wall-clock timezone.

## Example

```c
UNSIGNED8 t[16]; UNSIGNED16 n = sizeof(t);
AdsGetFileTime(hConn, (UNSIGNED8*)"x.txt", t, &n);
```

## See Also

- [AdsGetFileDate]({{ site.baseurl }}/en/functions/ads-get-file-date/)
- [AdsGetFileSize]({{ site.baseurl }}/en/functions/ads-get-file-size/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
