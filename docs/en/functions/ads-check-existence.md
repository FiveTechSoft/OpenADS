---
title: AdsCheckExistence
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-check-existence/
---

# AdsCheckExistence

Tests whether a file exists under the connection data directory (local or remote server data root).

Harbour-style name: `oads_File` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsCheckExistence(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                             UNSIGNED16 *pbExists);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle. Zero uses the last AdsConnect / rddads default. |
| `pucName` | `UNSIGNED8*` | Relative file name (or path under the data root). |
| `pbExists` | `UNSIGNED16*` | Out: 1 if the file exists, 0 otherwise. |

## Return Value

`AE_SUCCESS` (0). `AE_ACCESS_DENIED` (7079) if remote `EnableFileFunc` is off or the path escapes the jail. `AE_INVALID_CONNECTION_HANDLE` if no connection.

## Description

Paths are **jailed** under the connection data directory. On a remote connection the check runs on the server (requires `EnableFileFunc=1`). Absolute client paths are folded under the data root.

## Example

```c
UNSIGNED16 exists = 0;
AdsCheckExistence(hConn, (UNSIGNED8*)"orders.dbf", &exists);
if (exists) { /* ... */ }
```

## See Also

- [AdsDeleteFile]({{ site.baseurl }}/en/functions/ads-delete-file/)
- [AdsGetFileSize]({{ site.baseurl }}/en/functions/ads-get-file-size/)
- [AdsDirectory]({{ site.baseurl }}/en/functions/ads-directory/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
