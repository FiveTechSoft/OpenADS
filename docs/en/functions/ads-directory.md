---
title: AdsDirectory
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-directory/
---

# AdsDirectory

Lists files matching a mask under the data directory into a packed buffer.

Harbour-style name: `oads_Directory` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsDirectory(ADSHANDLE hConnect, UNSIGNED8 *pucMask,
                        UNSIGNED16 usAttr, UNSIGNED8 *pucBuffer,
                        UNSIGNED32 *pulBufLen);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucMask` | `UNSIGNED8*` | Mask such as `"*.dbf"` or `"subdir/*.*"`. |
| `usAttr` | `UNSIGNED16` | Attribute filter (reserved in v1; pass 0). |
| `pucBuffer` | `UNSIGNED8*` | Output buffer, or NULL to probe size. |
| `pulBufLen` | `UNSIGNED32*` | In: capacity. Out: bytes needed or written. |

## Return Value

`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` (5051) when probing or when the buffer is too small.

## Description

Each entry is packed as:

```
[u16 nameLen LE][name bytes]
[u64 size LE]
[u16 year][u8 mon][u8 day][u8 hh][u8 mm][u8 ss]
[u32 attr LE]   // bit 0x10 = directory
```

Typical pattern: call with `pucBuffer == NULL` to get `*pulBufLen`, allocate, call again.

## Example

```c
UNSIGNED32 need = 0;
AdsDirectory(hConn, (UNSIGNED8*)"inbox/*.*", 0, NULL, &need);
UNSIGNED8 *buf = (UNSIGNED8*)malloc(need);
AdsDirectory(hConn, (UNSIGNED8*)"inbox/*.*", 0, buf, &need);
```

## See Also

- [AdsDirExist]({{ site.baseurl }}/en/functions/ads-dir-exist/)
- [AdsCheckExistence]({{ site.baseurl }}/en/functions/ads-check-existence/)
- [AdsDirMake]({{ site.baseurl }}/en/functions/ads-dir-make/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
