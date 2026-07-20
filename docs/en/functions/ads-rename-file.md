---
title: AdsRenameFile
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-rename-file/
---

# AdsRenameFile

Renames or moves a file within the data-directory jail.

Harbour-style name: `oads_FRename` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsRenameFile(ADSHANDLE hConnect, UNSIGNED8 *pucOld,
                         UNSIGNED8 *pucNew);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucOld` | `UNSIGNED8*` | Existing relative path. |
| `pucNew` | `UNSIGNED8*` | New relative path (must stay under the data root). |

## Return Value

`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if source missing. `AE_ACCESS_DENIED` for jail escape or remote gate off.

## Description

Both paths are resolved through the same path jail. Cannot be used to escape the server data directory.

## Example

```c
AdsRenameFile(hConn, (UNSIGNED8*)"a.txt", (UNSIGNED8*)"b.txt");
```

## See Also

- [AdsDeleteFile]({{ site.baseurl }}/en/functions/ads-delete-file/)
- [AdsCheckExistence]({{ site.baseurl }}/en/functions/ads-check-existence/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
