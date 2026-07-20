---
title: AdsDirExist
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-dir-exist/
---

# AdsDirExist

Tests whether a directory exists under the data root.

Harbour-style name: `oads_DirExist` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsDirExist(ADSHANDLE hConnect, UNSIGNED8 *pucPath,
                       UNSIGNED16 *pbExists);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucPath` | `UNSIGNED8*` | Relative directory path. |
| `pbExists` | `UNSIGNED16*` | Out: 1 if directory exists, else 0. |

## Return Value

`AE_SUCCESS` (0). `AE_ACCESS_DENIED` if jail escape / remote gate off.

## Description

Unlike `AdsCheckExistence`, this only returns true for directories.

## Example

```c
UNSIGNED16 ok = 0;
AdsDirExist(hConn, (UNSIGNED8*)"inbox", &ok);
```

## See Also

- [AdsDirMake]({{ site.baseurl }}/en/functions/ads-dir-make/)
- [AdsDirRemove]({{ site.baseurl }}/en/functions/ads-dir-remove/)
- [AdsDirectory]({{ site.baseurl }}/en/functions/ads-directory/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
