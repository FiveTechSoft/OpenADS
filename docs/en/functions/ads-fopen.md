---
title: AdsFOpen
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fopen/
---

# AdsFOpen

Opens an existing file under the data root and returns a file handle.

Harbour-style name: `oads_FOpen` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFOpen(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                    UNSIGNED16 usMode, ADSHANDLE *phFile);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative file path. |
| `usMode` | `UNSIGNED16` | 0=read, 1=write, 2=read/write (`ADS_FO_*`). |
| `phFile` | `ADSHANDLE*` | Out: file handle for FRead/FWrite/FSeek/FClose. |

## Return Value

`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if missing. Remote: max 32 open files per session.

## Description

Handle is local or remote (server-side id). Always call `AdsFClose` when done. Disconnect closes remaining handles.

## Example

```c
ADSHANDLE hFile = 0;
AdsFOpen(hConn, (UNSIGNED8*)"inbox/hello.txt", 0 /*read*/, &hFile);
```

## See Also

- [AdsFCreate]({{ site.baseurl }}/en/functions/ads-fcreate/)
- [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/)
- [AdsFRead]({{ site.baseurl }}/en/functions/ads-fread/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
