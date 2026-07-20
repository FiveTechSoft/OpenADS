---
title: AdsFSeek
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fseek/
---

# AdsFSeek

Moves the file pointer and returns the new absolute position.

Harbour-style name: `oads_FSeek` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFSeek(ADSHANDLE hFile, SIGNED32 lOffset,
                    UNSIGNED16 usOrigin, UNSIGNED32 *pulPos);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hFile` | `ADSHANDLE` | File handle. |
| `lOffset` | `SIGNED32` | Byte offset (signed). |
| `usOrigin` | `UNSIGNED16` | 0=begin, 1=current, 2=end. |
| `pulPos` | `UNSIGNED32*` | Out: new position from start of file. |

## Return Value

`AE_SUCCESS` (0).

## Description

v1 uses 32-bit positions (files larger than 2 GiB for seek are not a target).

## Example

```c
UNSIGNED32 pos = 0;
AdsFSeek(hFile, 0, 2 /*end*/, &pos);
```

## See Also

- [AdsFRead]({{ site.baseurl }}/en/functions/ads-fread/)
- [AdsFWrite]({{ site.baseurl }}/en/functions/ads-fwrite/)
- [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
