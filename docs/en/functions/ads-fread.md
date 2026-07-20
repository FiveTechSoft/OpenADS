---
title: AdsFRead
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fread/
---

# AdsFRead

Reads up to `ulLen` bytes from the current file position.

Harbour-style name: `oads_FRead` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFRead(ADSHANDLE hFile, void *pBuf,
                    UNSIGNED32 ulLen, UNSIGNED32 *pulRead);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hFile` | `ADSHANDLE` | File handle. |
| `pBuf` | `void*` | Destination buffer. |
| `ulLen` | `UNSIGNED32` | Maximum bytes to read (remote chunk capped at 1 MiB). |
| `pulRead` | `UNSIGNED32*` | Out: bytes actually read (0 at EOF). |

## Return Value

`AE_SUCCESS` (0).

## Description

Loop for large transfers. Pair with `AdsFSeek` for random access.

## Example

```c
char buf[256]; UNSIGNED32 n = 0;
AdsFRead(hFile, buf, sizeof(buf), &n);
```

## See Also

- [AdsFWrite]({{ site.baseurl }}/en/functions/ads-fwrite/)
- [AdsFSeek]({{ site.baseurl }}/en/functions/ads-fseek/)
- [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
