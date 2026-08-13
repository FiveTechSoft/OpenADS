---
title: Server filesystem
layout: default
parent: Home (EN)
nav_order: 8
permalink: /en/server-filesystem/
---

# Server filesystem API (`oads_*` / `Ads*`)

OpenADS can manage files and directories **under the server data root**
from a client that has no mapped drive — the same problem LetoDB solves
with `leto_*`, using OpenADS names:

- **Harbour-style names:** `oads_File`, `oads_FErase`, … (aliases over ACE)
- **C / ACE:** the `Ads*` functions listed below

Introduced in **v1.8.18**.

## Security

| Setting | Default | Meaning |
|---------|---------|---------|
| `EnableFileFunc` / `--enable-file-func` | **off** | Remote filesystem opcodes denied (`AE_ACCESS_DENIED` = 7079) |

Paths are always resolved under the connection data directory (or
`openads_serverd --data` roots). `..` and absolute paths outside the jail
are rejected. Absolute client paths are **folded** under the data root
(same idea as remote `AdsCreateTable`).

**Warning:** with `EnableFileFunc=1`, any client that can connect may
read/write/delete under the data root. Use only on trusted networks
or with server login credentials.

## Config

```ini
# openads.ini
data = C:/OpenADS/data
EnableFileFunc = 1
```

```text
openads_serverd --data C:/OpenADS/data --enable-file-func --port 6262
```

## Function reference

### Meta / directory

| Function | Harbour-style | Description |
|----------|---------------|-------------|
| [AdsCheckExistence]({{ site.baseurl }}/en/functions/ads-check-existence/) | `oads_File` | File exists? |
| [AdsDeleteFile]({{ site.baseurl }}/en/functions/ads-delete-file/) | `oads_FErase` | Delete a file |
| [AdsRenameFile]({{ site.baseurl }}/en/functions/ads-rename-file/) | `oads_FRename` | Rename / move within jail |
| [AdsGetFileSize]({{ site.baseurl }}/en/functions/ads-get-file-size/) | `oads_FSize` | Size in bytes |
| [AdsGetFileTime]({{ site.baseurl }}/en/functions/ads-get-file-time/) | `oads_FTime` | Last modified time `"hh:mm:ss"` |
| [AdsGetFileDate]({{ site.baseurl }}/en/functions/ads-get-file-date/) | `oads_FDate` | Last modified date `"YYYYMMDD"` |
| [AdsDirectory]({{ site.baseurl }}/en/functions/ads-directory/) | `oads_Directory` | List files (packed buffer) |
| [AdsDirExist]({{ site.baseurl }}/en/functions/ads-dir-exist/) | `oads_DirExist` | Directory exists? |
| [AdsDirMake]({{ site.baseurl }}/en/functions/ads-dir-make/) | `oads_DirMake` | Create directory (parents OK) |
| [AdsDirRemove]({{ site.baseurl }}/en/functions/ads-dir-remove/) | `oads_DirRemove` | Remove empty directory |

### Low-level I/O

| Function | Harbour-style | Description |
|----------|---------------|-------------|
| [AdsFOpen]({{ site.baseurl }}/en/functions/ads-fopen/) | `oads_FOpen` | Open existing file → handle |
| [AdsFCreate]({{ site.baseurl }}/en/functions/ads-fcreate/) | `oads_FCreate` | Create/truncate file → handle |
| [AdsFClose]({{ site.baseurl }}/en/functions/ads-fclose/) | `oads_FClose` | Close handle |
| [AdsFRead]({{ site.baseurl }}/en/functions/ads-fread/) | `oads_FRead` | Read bytes |
| [AdsFWrite]({{ site.baseurl }}/en/functions/ads-fwrite/) | `oads_FWrite` | Write bytes |
| [AdsFSeek]({{ site.baseurl }}/en/functions/ads-fseek/) | `oads_FSeek` | Seek (origin 0/1/2) |

### Open modes (`AdsFOpen` / `usMode`)

| Constant (OpenADS) | Value | Meaning |
|--------------------|-------|---------|
| `ADS_FO_READ` | `0` | Read only |
| `ADS_FO_WRITE` | `1` | Write (read/write stream) |
| `ADS_FO_READWRITE` | `2` | Read/write |

### Seek origins (`AdsFSeek` / `usOrigin`)

| Value | Meaning |
|-------|---------|
| `0` | Beginning of file |
| `1` | Current position |
| `2` | End of file |

## Limits (v1)

| Limit | Value |
|-------|-------|
| Max open files per remote session | 32 |
| Max single `FRead` / `FWrite` chunk | 1 MiB (and within wire frame cap) |
| Handles on disconnect | closed automatically |

## Local vs remote

| Connection | Behaviour |
|------------|-----------|
| `tcp://` / `tls://` remote | Wire opcodes; server enforces `EnableFileFunc` + jail |
| Local data directory connection | Same `Ads*` API; paths jailed under that connection’s data dir |

`hConnect = 0` uses the last rddads / `AdsConnect60` default connection.

## Example (C)

```c
ADSHANDLE hConn = 0, hFile = 0;
UNSIGNED32 n = 0;
UNSIGNED16 exists = 0;

AdsConnect60((UNSIGNED8*)"tcp://192.168.1.10:6262//data",
             ADS_REMOTE_SERVER, NULL, NULL, 0, &hConn);

AdsDirMake(hConn, (UNSIGNED8*)"inbox");
AdsFCreate(hConn, (UNSIGNED8*)"inbox/hello.txt", 0, &hFile);
AdsFWrite(hFile, "hello", 5, &n);
AdsFClose(hFile);

AdsCheckExistence(hConn, (UNSIGNED8*)"inbox/hello.txt", &exists);
AdsDeleteFile(hConn, (UNSIGNED8*)"inbox/hello.txt");
AdsDirRemove(hConn, (UNSIGNED8*)"inbox");
AdsDisconnect(hConn);
```

## Upgrade

Remote ops need **both**:

1. Client `ace32.dll` / `ace64.dll` (v1.8.18+)
2. `openads_serverd` (v1.8.18+) with `EnableFileFunc` enabled when desired

## Table name resolution

OpenADS resolves table filenames **case-insensitively by default**. This
means `USE "Clientes"`, `USE "CLIENTES"`, and `USE "clientes"` all open
the same file if it exists on disk. No configuration is required.

- On **Windows**, the filesystem itself is case-insensitive
- On **Linux/macOS**, `platform::resolve_case_insensitive()` scans the
  parent directory and matches by lowercased comparison

This applies to all table opens: `AdsOpenTable`, `AdsCreateTable`, and
SQL `SELECT FROM`. Data Dictionary aliases remain case-sensitive.

## See also

- [What's New — v1.8.18]({{ site.baseurl }}/en/whatsnew/)
- [API Reference]({{ site.baseurl }}/en/api-reference/)
- Design: `docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`
- Harbour notes: `examples/harbour/oads_fs/`
