---
title: AdsDirRemove
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-dir-remove/
---

# AdsDirRemove

Removes an **empty** directory under the data root.

Harbour-style name: `oads_DirRemove` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsDirRemove(ADSHANDLE hConnect, UNSIGNED8 *pucPath);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucPath` | `UNSIGNED8*` | Relative directory path. |

## Return Value

`AE_SUCCESS` (0). Error if not empty or not a directory.

## Description

Non-recursive. Delete files first with `AdsDeleteFile`.

## Example

```c
AdsDirRemove(hConn, (UNSIGNED8*)"inbox");
```

## See Also

- [AdsDirMake]({{ site.baseurl }}/en/functions/ads-dir-make/)
- [AdsDeleteFile]({{ site.baseurl }}/en/functions/ads-delete-file/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
