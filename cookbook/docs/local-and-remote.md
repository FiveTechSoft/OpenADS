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
  (e.g. `--data "C:\data;D:\more-data"`) when the tables you want to
  serve live under different drives or shares -- a client path only has
  to fall under one of the listed roots.

The server can also be installed as a system service (a Windows service,
a `systemd` unit, or a `launchd` job), but you do not need that to run the
cookbook examples -- leave it running in a console window.

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
