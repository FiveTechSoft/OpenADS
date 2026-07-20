# `oads_*` — server filesystem helpers

Harbour-friendly names for OpenADS remote/local filesystem ops under the
connection data directory (LetoDB-style, without `leto_*` symbols).

## Server config

```ini
# openads.ini — required for remote clients
EnableFileFunc = 1
data = /path/to/data
```

Or CLI: `openads_serverd --data /path/to/data --enable-file-func`

Default is **off** (`AE_ACCESS_DENIED`).

## API (via ACE — call from C/Harbour after linking `ace32`/`ace64`)

| Harbour-style name | ACE |
|---|---|
| `oads_File` | `AdsCheckExistence` |
| `oads_FErase` | `AdsDeleteFile` |
| `oads_FRename` | `AdsRenameFile` |
| `oads_FSize` | `AdsGetFileSize` |
| `oads_FTime` / `oads_FDate` | `AdsGetFileTime` / `AdsGetFileDate` |
| `oads_Directory` | `AdsDirectory` |
| `oads_DirExist` / `DirMake` / `DirRemove` | `AdsDirExist` / `AdsDirMake` / `AdsDirRemove` |
| `oads_FOpen` … `FSeek` | `AdsFOpen` … `AdsFSeek` |

Thin `HB_FUNC` wrappers can be added in `oads_fs_c.c` (optional); apps may
also call the `Ads*` exports directly from Harbour via the existing rddads
/ ACE import lib.

## Example

```harbour
AdsConnect60( "tcp://host:16262//data", 2, NIL, NIL, 0, @h )
AdsDirMake( h, "inbox" )
AdsFCreate( h, "inbox/hello.txt", 0, @hf )
AdsFWrite( hf, "hello", 5, @n )
AdsFClose( hf )
AdsCheckExistence( h, "inbox/hello.txt", @ex )
```

See `docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`.
