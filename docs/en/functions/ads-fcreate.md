---
title: AdsFCreate
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fcreate/
---

# AdsFCreate

Creates or truncates a file under the data root and returns a writable handle.

Harbour-style name: `oads_FCreate` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFCreate(ADSHANDLE hConnect, UNSIGNED8 *pucName,
                      UNSIGNED16 usAttribute, ADSHANDLE *phFile);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative file path. |
| `usAttribute` | `UNSIGNED16` | Reserved (pass 0). |
| `phFile` | `ADSHANDLE*` | Out: file handle. |

## Return Value

`AE_SUCCESS` (0).

## Description

Parent directories should already exist (or create them with `AdsDirMake`). Truncates if the file already exists.

## Example

```c
ADSHANDLE hFile = 0;
AdsFCreate(hConn, (UNSIGNED8*)"inbox/new.txt", 0, &hFile);
```

## See Also

- [AdsFOpen]({{ site.baseurl }}/en/functions/ads-fopen/)
- [AdsFWrite]({{ site.baseurl }}/en/functions/ads-fwrite/)
- [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
