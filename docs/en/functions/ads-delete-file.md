---
title: AdsDeleteFile
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-delete-file/
---

# AdsDeleteFile

Deletes a file under the connection data directory (local or remote).

Harbour-style name: `oads_FErase` (see [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)).

## Syntax

```c
UNSIGNED32 AdsDeleteFile(ADSHANDLE hConnect, UNSIGNED8 *pucName);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Connection handle (0 = default). |
| `pucName` | `UNSIGNED8*` | Relative path of the file to delete. |

## Return Value

`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` (5018) if missing. `AE_ACCESS_DENIED` if remote file functions are disabled or the path escapes the jail.

## Description

Does not delete directories (use `AdsDirRemove`). Remote delete requires `EnableFileFunc=1` on `openads_serverd`.

## Example

```c
AdsDeleteFile(hConn, (UNSIGNED8*)"inbox/old.txt");
```

## See Also

- [AdsCheckExistence]({{ site.baseurl }}/en/functions/ads-check-existence/)
- [AdsRenameFile]({{ site.baseurl }}/en/functions/ads-rename-file/)
- [AdsDirRemove]({{ site.baseurl }}/en/functions/ads-dir-remove/)
- [Server filesystem]({{ site.baseurl }}/en/server-filesystem/)
- [What's New v1.8.18]({{ site.baseurl }}/en/whatsnew/)
