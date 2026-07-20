# Server filesystem API (`oads_*` / `Ads*`)

OpenADS can manage files and directories **under the server data root**
from a client that has no mapped drive — the same problem LetoDB solves
with `leto_*`, using OpenADS names:

- Harbour style: `oads_*` (documented alias names)
- C / ACE: `AdsCheckExistence`, `AdsDeleteFile`, `AdsRenameFile`,
  `AdsGetFileSize`, `AdsGetFileTime`, `AdsGetFileDate`, `AdsDirectory`,
  `AdsDirExist`, `AdsDirMake`, `AdsDirRemove`, `AdsFOpen` / `FCreate` /
  `FClose` / `FRead` / `FWrite` / `FSeek`

## Security

| Setting | Default | Meaning |
|---------|---------|---------|
| `EnableFileFunc` / `--enable-file-func` | **off** | Remote filesystem opcodes denied (`AE_ACCESS_DENIED` 7079) |

Paths are always resolved under the connection data directory (or
`openads_serverd --data` roots). `..` and absolute paths outside the jail
are rejected. Absolute client paths are **folded** under the data root
(same idea as remote `AdsCreateTable`).

**Warning:** with `EnableFileFunc=1`, any authenticated client can
read/write/delete files under the data root. Use only on trusted networks
or with server credentials.

## Config

```ini
# openads.ini
data = C:/OpenADS/data
EnableFileFunc = 1
```

```text
openads_serverd --data C:/OpenADS/data --enable-file-func --port 6262
```

## Upgrade

Remote ops need **both**:

1. Client `ace32.dll` / `ace64.dll` with the new exports  
2. `openads_serverd` with filesystem opcodes and `EnableFileFunc`

## See also

- Design: `docs/superpowers/specs/2026-07-20-oads-server-filesystem-design.md`
- Example notes: `examples/harbour/oads_fs/README.md`
