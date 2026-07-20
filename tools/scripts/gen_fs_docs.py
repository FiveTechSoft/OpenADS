# Generate EN function pages for server filesystem API (v1.8.18).
from pathlib import Path

BASE = Path(__file__).resolve().parents[2] / "docs" / "en" / "functions"

SLUG = {
    "AdsCheckExistence": "ads-check-existence",
    "AdsDeleteFile": "ads-delete-file",
    "AdsRenameFile": "ads-rename-file",
    "AdsGetFileSize": "ads-get-file-size",
    "AdsGetFileTime": "ads-get-file-time",
    "AdsGetFileDate": "ads-get-file-date",
    "AdsDirectory": "ads-directory",
    "AdsDirExist": "ads-dir-exist",
    "AdsDirMake": "ads-dir-make",
    "AdsDirRemove": "ads-dir-remove",
    "AdsFOpen": "ads-fopen",
    "AdsFCreate": "ads-fcreate",
    "AdsFClose": "ads-fclose",
    "AdsFRead": "ads-fread",
    "AdsFWrite": "ads-fwrite",
    "AdsFSeek": "ads-fseek",
}

FUNCS = [
    {
        "title": "AdsCheckExistence",
        "oads": "oads_File",
        "summary": "Tests whether a file exists under the connection data directory (local or remote server data root).",
        "sig": "UNSIGNED32 AdsCheckExistence(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                             UNSIGNED16 *pbExists);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle. Zero uses the last AdsConnect / rddads default."),
            ("pucName", "UNSIGNED8*", "Relative file name (or path under the data root)."),
            ("pbExists", "UNSIGNED16*", "Out: 1 if the file exists, 0 otherwise."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_ACCESS_DENIED` (7079) if remote `EnableFileFunc` is off or the path escapes the jail. `AE_INVALID_CONNECTION_HANDLE` if no connection.",
        "desc": "Paths are **jailed** under the connection data directory. On a remote connection the check runs on the server (requires `EnableFileFunc=1`). Absolute client paths are folded under the data root.",
        "example": 'UNSIGNED16 exists = 0;\n'
                   'AdsCheckExistence(hConn, (UNSIGNED8*)"orders.dbf", &exists);\n'
                   "if (exists) { /* ... */ }",
        "also": ["AdsDeleteFile", "AdsGetFileSize", "AdsDirectory"],
    },
    {
        "title": "AdsDeleteFile",
        "oads": "oads_FErase",
        "summary": "Deletes a file under the connection data directory (local or remote).",
        "sig": "UNSIGNED32 AdsDeleteFile(ADSHANDLE hConnect, UNSIGNED8 *pucName);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative path of the file to delete."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` (5018) if missing. `AE_ACCESS_DENIED` if remote file functions are disabled or the path escapes the jail.",
        "desc": "Does not delete directories (use `AdsDirRemove`). Remote delete requires `EnableFileFunc=1` on `openads_serverd`.",
        "example": 'AdsDeleteFile(hConn, (UNSIGNED8*)"inbox/old.txt");',
        "also": ["AdsCheckExistence", "AdsRenameFile", "AdsDirRemove"],
    },
    {
        "title": "AdsRenameFile",
        "oads": "oads_FRename",
        "summary": "Renames or moves a file within the data-directory jail.",
        "sig": "UNSIGNED32 AdsRenameFile(ADSHANDLE hConnect, UNSIGNED8 *pucOld,\n"
               "                         UNSIGNED8 *pucNew);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucOld", "UNSIGNED8*", "Existing relative path."),
            ("pucNew", "UNSIGNED8*", "New relative path (must stay under the data root)."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if source missing. `AE_ACCESS_DENIED` for jail escape or remote gate off.",
        "desc": "Both paths are resolved through the same path jail. Cannot be used to escape the server data directory.",
        "example": 'AdsRenameFile(hConn, (UNSIGNED8*)"a.txt", (UNSIGNED8*)"b.txt");',
        "also": ["AdsDeleteFile", "AdsCheckExistence"],
    },
    {
        "title": "AdsGetFileSize",
        "oads": "oads_FSize",
        "summary": "Returns the size in bytes of a file under the data directory.",
        "sig": "UNSIGNED32 AdsGetFileSize(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                          UNSIGNED32 *pulSize);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative file path."),
            ("pulSize", "UNSIGNED32*", "Out: file size in bytes (v1 capped at 4 GiB−1)."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if not a regular file. `AE_INTERNAL_ERROR` if size exceeds `UNSIGNED32`.",
        "desc": "For remote connections the size is read on the server.",
        "example": 'UNSIGNED32 sz = 0;\n'
                   'AdsGetFileSize(hConn, (UNSIGNED8*)"inbox/hello.txt", &sz);',
        "also": ["AdsGetFileTime", "AdsGetFileDate", "AdsCheckExistence"],
    },
    {
        "title": "AdsGetFileTime",
        "oads": "oads_FTime",
        "summary": 'Returns the last-modified time of a file as `"hh:mm:ss"` (local server clock).',
        "sig": "UNSIGNED32 AdsGetFileTime(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                          UNSIGNED8 *pucTime, UNSIGNED16 *pusLen);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative file path."),
            ("pucTime", "UNSIGNED8*", "Buffer for the time string (null-terminated)."),
            ("pusLen", "UNSIGNED16*", "In: buffer capacity. Out: length written (including NUL). Need at least 9."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` (5051) if the buffer is too small (`*pusLen` set to required size).",
        "desc": "Uses the server (or local process) wall-clock timezone.",
        "example": "UNSIGNED8 t[16]; UNSIGNED16 n = sizeof(t);\n"
                   'AdsGetFileTime(hConn, (UNSIGNED8*)"x.txt", t, &n);',
        "also": ["AdsGetFileDate", "AdsGetFileSize"],
    },
    {
        "title": "AdsGetFileDate",
        "oads": "oads_FDate",
        "summary": 'Returns the last-modified date of a file as `"YYYYMMDD"`.',
        "sig": "UNSIGNED32 AdsGetFileDate(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                          UNSIGNED8 *pucDate, UNSIGNED16 *pusLen);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative file path."),
            ("pucDate", "UNSIGNED8*", "Buffer for the date string."),
            ("pusLen", "UNSIGNED16*", "In/out length; need at least 9."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` if too small.",
        "desc": 'Pair with `AdsGetFileTime` for full mtime. Harbour wrappers can convert `"YYYYMMDD"` to a date value.',
        "example": "UNSIGNED8 d[16]; UNSIGNED16 n = sizeof(d);\n"
                   'AdsGetFileDate(hConn, (UNSIGNED8*)"x.txt", d, &n);',
        "also": ["AdsGetFileTime", "AdsGetFileSize"],
    },
    {
        "title": "AdsDirectory",
        "oads": "oads_Directory",
        "summary": "Lists files matching a mask under the data directory into a packed buffer.",
        "sig": "UNSIGNED32 AdsDirectory(ADSHANDLE hConnect, UNSIGNED8 *pucMask,\n"
               "                        UNSIGNED16 usAttr, UNSIGNED8 *pucBuffer,\n"
               "                        UNSIGNED32 *pulBufLen);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucMask", "UNSIGNED8*", 'Mask such as `"*.dbf"` or `"subdir/*.*"`.'),
            ("usAttr", "UNSIGNED16", "Attribute filter (reserved in v1; pass 0)."),
            ("pucBuffer", "UNSIGNED8*", "Output buffer, or NULL to probe size."),
            ("pulBufLen", "UNSIGNED32*", "In: capacity. Out: bytes needed or written."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_INSUFFICIENT_BUFFER` (5051) when probing or when the buffer is too small.",
        "desc": """Each entry is packed as:

```
[u16 nameLen LE][name bytes]
[u64 size LE]
[u16 year][u8 mon][u8 day][u8 hh][u8 mm][u8 ss]
[u32 attr LE]   // bit 0x10 = directory
```

Typical pattern: call with `pucBuffer == NULL` to get `*pulBufLen`, allocate, call again.""",
        "example": "UNSIGNED32 need = 0;\n"
                   'AdsDirectory(hConn, (UNSIGNED8*)"inbox/*.*", 0, NULL, &need);\n'
                   "UNSIGNED8 *buf = (UNSIGNED8*)malloc(need);\n"
                   'AdsDirectory(hConn, (UNSIGNED8*)"inbox/*.*", 0, buf, &need);',
        "also": ["AdsDirExist", "AdsCheckExistence", "AdsDirMake"],
    },
    {
        "title": "AdsDirExist",
        "oads": "oads_DirExist",
        "summary": "Tests whether a directory exists under the data root.",
        "sig": "UNSIGNED32 AdsDirExist(ADSHANDLE hConnect, UNSIGNED8 *pucPath,\n"
               "                       UNSIGNED16 *pbExists);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucPath", "UNSIGNED8*", "Relative directory path."),
            ("pbExists", "UNSIGNED16*", "Out: 1 if directory exists, else 0."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_ACCESS_DENIED` if jail escape / remote gate off.",
        "desc": "Unlike `AdsCheckExistence`, this only returns true for directories.",
        "example": "UNSIGNED16 ok = 0;\n"
                   'AdsDirExist(hConn, (UNSIGNED8*)"inbox", &ok);',
        "also": ["AdsDirMake", "AdsDirRemove", "AdsDirectory"],
    },
    {
        "title": "AdsDirMake",
        "oads": "oads_DirMake",
        "summary": "Creates a directory under the data root (including intermediate parents).",
        "sig": "UNSIGNED32 AdsDirMake(ADSHANDLE hConnect, UNSIGNED8 *pucPath);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucPath", "UNSIGNED8*", "Relative directory path to create."),
        ],
        "returns": "`AE_SUCCESS` (0). Succeeds if the directory already exists.",
        "desc": "Remote create requires `EnableFileFunc=1`.",
        "example": 'AdsDirMake(hConn, (UNSIGNED8*)"inbox/2026");',
        "also": ["AdsDirRemove", "AdsDirExist"],
    },
    {
        "title": "AdsDirRemove",
        "oads": "oads_DirRemove",
        "summary": "Removes an **empty** directory under the data root.",
        "sig": "UNSIGNED32 AdsDirRemove(ADSHANDLE hConnect, UNSIGNED8 *pucPath);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucPath", "UNSIGNED8*", "Relative directory path."),
        ],
        "returns": "`AE_SUCCESS` (0). Error if not empty or not a directory.",
        "desc": "Non-recursive. Delete files first with `AdsDeleteFile`.",
        "example": 'AdsDirRemove(hConn, (UNSIGNED8*)"inbox");',
        "also": ["AdsDirMake", "AdsDeleteFile"],
    },
    {
        "title": "AdsFOpen",
        "oads": "oads_FOpen",
        "summary": "Opens an existing file under the data root and returns a file handle.",
        "sig": "UNSIGNED32 AdsFOpen(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                    UNSIGNED16 usMode, ADSHANDLE *phFile);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative file path."),
            ("usMode", "UNSIGNED16", "0=read, 1=write, 2=read/write (`ADS_FO_*`)."),
            ("phFile", "ADSHANDLE*", "Out: file handle for FRead/FWrite/FSeek/FClose."),
        ],
        "returns": "`AE_SUCCESS` (0). `AE_NO_FILE_FOUND` if missing. Remote: max 32 open files per session.",
        "desc": "Handle is local or remote (server-side id). Always call `AdsFClose` when done. Disconnect closes remaining handles.",
        "example": "ADSHANDLE hFile = 0;\n"
                   'AdsFOpen(hConn, (UNSIGNED8*)"inbox/hello.txt", 0 /*read*/, &hFile);',
        "also": ["AdsFCreate", "AdsFClose", "AdsFRead"],
    },
    {
        "title": "AdsFCreate",
        "oads": "oads_FCreate",
        "summary": "Creates or truncates a file under the data root and returns a writable handle.",
        "sig": "UNSIGNED32 AdsFCreate(ADSHANDLE hConnect, UNSIGNED8 *pucName,\n"
               "                      UNSIGNED16 usAttribute, ADSHANDLE *phFile);",
        "params": [
            ("hConnect", "ADSHANDLE", "Connection handle (0 = default)."),
            ("pucName", "UNSIGNED8*", "Relative file path."),
            ("usAttribute", "UNSIGNED16", "Reserved (pass 0)."),
            ("phFile", "ADSHANDLE*", "Out: file handle."),
        ],
        "returns": "`AE_SUCCESS` (0).",
        "desc": "Parent directories should already exist (or create them with `AdsDirMake`). Truncates if the file already exists.",
        "example": "ADSHANDLE hFile = 0;\n"
                   'AdsFCreate(hConn, (UNSIGNED8*)"inbox/new.txt", 0, &hFile);',
        "also": ["AdsFOpen", "AdsFWrite", "AdsFClose"],
    },
    {
        "title": "AdsFClose",
        "oads": "oads_FClose",
        "summary": "Closes a file handle from `AdsFOpen` or `AdsFCreate`.",
        "sig": "UNSIGNED32 AdsFClose(ADSHANDLE hFile);",
        "params": [
            ("hFile", "ADSHANDLE", "File handle."),
        ],
        "returns": "`AE_SUCCESS` (0). Error if the handle is invalid.",
        "desc": "Invalidates the handle.",
        "example": "AdsFClose(hFile);",
        "also": ["AdsFOpen", "AdsFCreate"],
    },
    {
        "title": "AdsFRead",
        "oads": "oads_FRead",
        "summary": "Reads up to `ulLen` bytes from the current file position.",
        "sig": "UNSIGNED32 AdsFRead(ADSHANDLE hFile, void *pBuf,\n"
               "                    UNSIGNED32 ulLen, UNSIGNED32 *pulRead);",
        "params": [
            ("hFile", "ADSHANDLE", "File handle."),
            ("pBuf", "void*", "Destination buffer."),
            ("ulLen", "UNSIGNED32", "Maximum bytes to read (remote chunk capped at 1 MiB)."),
            ("pulRead", "UNSIGNED32*", "Out: bytes actually read (0 at EOF)."),
        ],
        "returns": "`AE_SUCCESS` (0).",
        "desc": "Loop for large transfers. Pair with `AdsFSeek` for random access.",
        "example": "char buf[256]; UNSIGNED32 n = 0;\n"
                   "AdsFRead(hFile, buf, sizeof(buf), &n);",
        "also": ["AdsFWrite", "AdsFSeek", "AdsFClose"],
    },
    {
        "title": "AdsFWrite",
        "oads": "oads_FWrite",
        "summary": "Writes `ulLen` bytes at the current file position.",
        "sig": "UNSIGNED32 AdsFWrite(ADSHANDLE hFile, const void *pBuf,\n"
               "                     UNSIGNED32 ulLen, UNSIGNED32 *pulWritten);",
        "params": [
            ("hFile", "ADSHANDLE", "File handle opened for write."),
            ("pBuf", "const void*", "Source buffer."),
            ("ulLen", "UNSIGNED32", "Bytes to write (remote chunk capped at 1 MiB)."),
            ("pulWritten", "UNSIGNED32*", "Out: bytes written."),
        ],
        "returns": "`AE_SUCCESS` (0).",
        "desc": "Data is flushed on the server after each write in v1.",
        "example": 'UNSIGNED32 n = 0;\nAdsFWrite(hFile, "hello", 5, &n);',
        "also": ["AdsFRead", "AdsFSeek", "AdsFClose"],
    },
    {
        "title": "AdsFSeek",
        "oads": "oads_FSeek",
        "summary": "Moves the file pointer and returns the new absolute position.",
        "sig": "UNSIGNED32 AdsFSeek(ADSHANDLE hFile, SIGNED32 lOffset,\n"
               "                    UNSIGNED16 usOrigin, UNSIGNED32 *pulPos);",
        "params": [
            ("hFile", "ADSHANDLE", "File handle."),
            ("lOffset", "SIGNED32", "Byte offset (signed)."),
            ("usOrigin", "UNSIGNED16", "0=begin, 1=current, 2=end."),
            ("pulPos", "UNSIGNED32*", "Out: new position from start of file."),
        ],
        "returns": "`AE_SUCCESS` (0).",
        "desc": "v1 uses 32-bit positions (files larger than 2 GiB for seek are not a target).",
        "example": "UNSIGNED32 pos = 0;\n"
                   "AdsFSeek(hFile, 0, 2 /*end*/, &pos);",
        "also": ["AdsFRead", "AdsFWrite", "AdsFClose"],
    },
]


def render(f: dict) -> str:
    slug = SLUG[f["title"]]
    params = "\n".join(
        f"| `{n}` | `{t}` | {d} |" for n, t, d in f["params"]
    )
    also = "\n".join(
        f"- [{name}]({{{{ site.baseurl }}}}/en/functions/{SLUG[name]}/)"
        for name in f["also"]
    )
    return f"""---
title: {f['title']}
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/{slug}/
---

# {f['title']}

{f['summary']}

Harbour-style name: `{f['oads']}` (see [Server filesystem]({{{{ site.baseurl }}}}/en/server-filesystem/)).

## Syntax

```c
{f['sig']}
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
{params}

## Return Value

{f['returns']}

## Description

{f['desc']}

## Example

```c
{f['example']}
```

## See Also

{also}
- [Server filesystem]({{{{ site.baseurl }}}}/en/server-filesystem/)
- [What's New v1.8.18]({{{{ site.baseurl }}}}/en/whatsnew/)
"""


def main() -> None:
    BASE.mkdir(parents=True, exist_ok=True)
    for f in FUNCS:
        slug = SLUG[f["title"]]
        path = BASE / f"{slug}.md"
        path.write_text(render(f), encoding="utf-8")
        print("wrote", path.relative_to(BASE.parent.parent.parent))


if __name__ == "__main__":
    main()
