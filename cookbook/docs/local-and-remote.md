# Local and remote

OpenADS can run two ways without changing your application code. The only
thing that differs is the **connection string** -- see
[`connection-strings.md`](connection-strings.md) for the exact format of
each one.

## In-process (local)

The engine runs **inside your program**. The OpenADS DLL itself is the
server -- there is no separate process and no network hop. This is the
fastest to set up and the fastest to run.

You are local when you point the connection at:

- a **folder path** for file tables (DBF/CDX/NTX or ADT/ADI/ADM), or
- a `sqlite://` file.

```harbour
AdsSetServerType( ADS_LOCAL_SERVER )
AdsSetFileType( ADS_CDX )
AdsConnect( "C:\data\app" )          // folder on this machine
```

Best for: development, single-user tools, embedded use, and any case
where the program and the data live on the same machine.

## Remote (separate server)

A separate `openads_serverd` process **owns the data**. Your program is a
client that connects to it over `tcp://` (or `tls://` for an encrypted
transport). Several clients can share one server.

The key point: **your application code does not change.** After the
connect call returns, every navigation and SQL call is identical to the
local path. Only the connection string is different.

Best for: many users sharing the same tables, putting the data on a
machine other than the client, or wanting a single guarded owner of the
files.

### Starting a server (generic)

A console run is enough for the examples:

```
openads_serverd --port 6262 --data <server-data-folder>
```

Optional:

- `--http-port 6263` -- a small browser console for inspecting the server.
- `--tls` flags -- enable the encrypted `tls://` transport.
- `--data <dir>` -- jail remote `Connect` requests under this directory
  (and, if `--http-port` is set, the directory the browser console
  serves). Accepts more than one root separated by `;`
  (e.g. `--data "C:/data;D:/more-data"`) when the tables you want to
  serve live under different drives or shares -- a client path only has
  to fall under one of the listed roots.
- `--legacy-paths` -- zero-change ERP mode: see
  [Path mapping: `--data`, URI, and `--legacy-paths`](#path-mapping---data-uri-and---legacy-paths)
  below.

Prefer **forward slashes** in `--data` and in the URI path on every OS
(including Windows). Backslashes are accepted but confuse shell quoting
and connection-string construction.

The server can also be installed as a system service (a Windows service,
a `systemd` unit, or a `launchd` job), but you do not need that to run the
cookbook examples -- leave it running in a console window.

### Path mapping: `--data`, URI, and `--legacy-paths`

Three separate pieces. Mixing them is the usual source of
`AdsConnect60 failed` and of tables landing on the wrong drive.

| Piece | What it is | Example |
|-------|------------|---------|
| `--data` | Physical directory **on the server** where files live (the jail) | `--data C:/Temp` |
| URI path | Connect directory the client asks for after `host:port/` | `tcp://127.0.0.1:6262/C:/` |
| `--legacy-paths` | Remount client-absolute table paths onto `--data` | flag or `legacy_paths=1` in `openads.ini` |

**Do not put the data root inside the URI when using legacy mode.**
`--data` already names the physical tree. The URI path is only the
*logical* connect directory the client still spells (often a drive root
such as `C:/` or `E:/`).

#### Strict mode (default, no `--legacy-paths`)

- The URI path must fall **under** one of the `--data` roots.
- Relative table names open under the connect directory.
- Absolute table paths that already exist on the **server** host may be
  opened as free tables (SAP-compatible). That is *not* remounting onto
  `--data`.

```
openads_serverd --port 6262 --data C:/Temp/app
# client:
AdsConnect60( "tcp://127.0.0.1:6262/C:/Temp/app", ADS_REMOTE_SERVER, ... )
USE "orders.dbf"   // -> C:/Temp/app/orders.dbf
```

Wrong (path outside `--data`):

```
--data C:/Temp
URI  tcp://127.0.0.1:6262/Temp/C:/     // fails: not under C:/Temp
URI  tcp://127.0.0.1:6262/C:/          // fails without legacy-paths
```

#### Legacy ERP mode (`--legacy-paths`) — zero client source changes

Use this when the application still does `USE "C:/Creative.RAM/..."` (or
`E:\...`) and you want those tables under a single server folder, or on
Linux/macOS without Windows drive letters.

```
# Physical layout on the server:
#   C:/Temp/Creative.RAM/CAC00001/...
#   C:/Temp/Creative.KTE/CAC00001/...

openads_serverd --port 6262 --data C:/Temp --legacy-paths

# client (unchanged USE lines):
AdsSetServerType( ADS_REMOTE_SERVER )
AdsConnect60( "tcp://127.0.0.1:6262/C:/", ADS_REMOTE_SERVER, ... )
USE "C:/Creative.RAM/CAC00001/TEST.dbf"
// opens C:/Temp/Creative.RAM/CAC00001/TEST.dbf
```

Rules with `--legacy-paths`:

1. Drive letter is ignored for matching; comparison is case-insensitive.
2. If the client path starts with a `--data` root (ignoring drive), the
   root prefix is stripped and re-joined under that root.
3. Otherwise the drive/root is dropped and the remainder is joined under
   the first `--data` root (`C:/Creative.RAM/...` → `<data>/Creative.RAM/...`).
4. A drive-root-only connect dir (`C:/`, `E:/`) maps to the matching
   drive root when `--data` lists whole drives, else to the first root.
5. Host files that happen to exist at the *client* spelling outside
   `--data` are **not** used; remount always wins.

Same idea Windows client → Linux server:

```
openads_serverd --port 16262 --data /tmp/openads_legacy --legacy-paths
# client URI:  tcp://server:16262/E:/
# client USE:  E:\CREATIVE.RAM\C0000001\TEST.dbf
# lands at:    /tmp/openads_legacy/CREATIVE.RAM/C0000001/TEST.dbf
```

#### Notation cheat sheet (forward slashes)

| Goal | Server | Client URI | Client table path |
|------|--------|------------|-------------------|
| Relative names under a folder | `--data C:/app/data` | `tcp://host:6262/C:/app/data` | `orders.dbf` |
| Whole drive as data | `--data C:/` | `tcp://host:6262/C:/` | `Temp/orders.dbf` or absolute under `C:/` |
| ERP absolute paths, data in Temp | `--data C:/Temp --legacy-paths` | `tcp://host:6262/C:/` | `C:/Creative.RAM/...` (unchanged) |
| ERP absolute paths on Linux | `--data /tmp/openads_legacy --legacy-paths` | `tcp://host:6262/E:/` | `E:/CREATIVE.RAM/...` |

`openads.ini` equivalents:

```ini
data = C:/Temp
legacy_paths = 1
port = 6262
```

**Common mistakes**

| Mistake | What happens |
|---------|----------------|
| `data = /Temp/C:` or URI `.../Temp/C:/` | Connect fails or Studio shows nonsense `data=/Temp/C:` — do not glue `--data` and the drive letter into one string |
| `--data C:/Temp` without `--legacy-paths`, URI `.../C:/` | Connect denied (path outside data directory) |
| `--data C:/Temp` without `--legacy-paths`, URI `.../C:/`, ADSCDX local | Connect may succeed as **local** and touch real `C:\...` — always set `ADS_REMOTE_SERVER` for remote |
| URI embeds the data folder in legacy mode (`.../Temp/C:/`) | Wrong; keep URI logical (`.../C:/`) and put the folder only in `--data` |
| Backslashes in the URI | Prefer `C:/` not `C:\` — `\` is an escape in many languages |

### The Harbour client difference

For remote **file tables**, select the remote server type, then connect to
the `tcp://` path:

```harbour
AdsSetServerType( ADS_REMOTE_SERVER )
AdsConnect60( "tcp://host:6262/app/data", ADS_REMOTE_SERVER, , , 0, @hConn )
// or simply:  AdsConnect( "tcp://host:6262/app/data" )
```

After this point your `AdsOpenTable`, `dbSeek`, `dbAppend`, SQL -- all of
it -- is exactly what you would write locally.

## SQL back-ends are already client/server

The SQL back-ends (PostgreSQL, MariaDB/MySQL, and anything reached through
ODBC) are **inherently client-to-server**. The remote database engine owns
the data and your program is always a client of it -- there is no separate
`openads_serverd` in the middle for these. You still only change the
connection string to point at them.

## At a glance

| Aspect | In-process (local) | Remote OpenADS server | SQL back-ends |
|--------|--------------------|-----------------------|---------------|
| Who owns the data | the DLL in your process | `openads_serverd` | the SQL engine |
| Separate process? | no | yes | yes (the database) |
| Network | none | `tcp://` or `tls://` | the engine's protocol |
| Connection string | folder or `sqlite://` | `tcp://...` / `tls://...` | `postgresql://`, `mariadb://`, `odbc://` |
| App code change | -- | none (only the URI) | none (only the URI) |
| Best for | dev, single user, embedded | shared file tables | shared SQL data |

See also [`../README.md`](../README.md) for the high-level picture and
[`building-and-running.md`](building-and-running.md) for how to compile
and run the examples.
