---
title: AdsFWrite
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-fwrite/
---

# AdsFWrite

Writes `ulLen` bytes at the current file position.

Harbour-style name: `oads_FWrite` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsFWrite(ADSHANDLE hFile, const void *pBuf,
                     UNSIGNED32 ulLen, UNSIGNED32 *pulWritten);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hFile` | `ADSHANDLE` | File handle opened for write. |
| `pBuf` | `const void*` | Source buffer. |
| `ulLen` | `UNSIGNED32` | Bytes to write (remote chunk capped at 1 MiB). |
| `pulWritten` | `UNSIGNED32*` | Out: bytes written. |

## Return Value

`AE_SUCCESS` (0).

## Description

Data is flushed on the server after each write in v1.

## Example

```c
UNSIGNED32 n = 0;
AdsFWrite(hFile, "hello", 5, &n);
```

## See Also

- [AdsFRead]({{ site.baseurl }}/en/functions/ads-fread/)
- [AdsFSeek]({{ site.baseurl }}/en/functions/ads-fseek/)
- [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
