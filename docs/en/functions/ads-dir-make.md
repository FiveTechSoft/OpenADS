---
title: AdsDirMake
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-dir-make/
---

# AdsDirMake

Creates a directory under the data root (including intermediate parents).

Harbour-style name: `oads_DirMake` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsDirMake(ADSHANDLE hConnect, UNSIGNED8 *pucPath);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucPath` | `UNSIGNED8*` | Relative directory path to create. |

## Return Value

`AE_SUCCESS` (0). Succeeds if the directory already exists.

## Description

Remote create requires `EnableFileFunc=1`.

## Example

```c
AdsDirMake(hConn, (UNSIGNED8*)"inbox/2026");
```

## See Also

- [AdsDirRemove]({{ site.baseurl }}/en/functions/ads-dir-remove/)
- [AdsDirExist]({{ site.baseurl }}/en/functions/ads-dir-exist/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
