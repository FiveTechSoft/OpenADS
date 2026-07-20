---
title: AdsFClose
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fclose/
---

# AdsFClose

Closes a file handle from `AdsFOpen` or `AdsFCreate`.

Harbour-style name: `oads_FClose` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFClose(ADSHANDLE hFile);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hFile` | `ADSHANDLE` | File handle. |

## Return Value

`AE_SUCCESS` (0). Error if the handle is invalid.

## Description

Invalidates the handle.

## Example

```c
AdsFClose(hFile);
```

## See Also

- [AdsFOpen]({{ site.baseurl }}/en/functions/ads-fopen/)
- [AdsFCreate]({{ site.baseurl }}/en/functions/ads-fcreate/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
