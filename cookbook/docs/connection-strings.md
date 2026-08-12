# Connection strings

OpenADS chooses the back-end from the **connection string** you pass to
the connect call. Same application code, different storage — you only
change this one string.

## The connect call

- **Pure Harbour (file tables):** the high-level RDD path uses
  `AdsConnect( <folder> )` after `AdsSetServerType()` / `AdsSetFileType()`.
- **Pure Harbour (any back-end, by URI):** the lower-level
  `AdsConnect60( <uri>, ADS_LOCAL_SERVER, <user>, <pass>, 0, @hConn )`
  returns a connection handle you pass to `AdsOpenTable( hConn, ... )`.
- **ORM:** `TORMConnection():New( <uri> )` (or `hbo_Connect( <uri> )`).
  The URI is exactly the same string in all cases.

## The schemes

### File tables — DBF / CDX / NTX (local)

A plain **folder path**. Tables are `*.dbf` files inside it; indexes are
`*.cdx` (compound) or `*.ntx`.

```
C:\data\app          (Windows)
/var/lib/app/data    (POSIX)
```

```harbour
AdsSetServerType( ADS_LOCAL_SERVER )
AdsSetFileType( ADS_CDX )         // .cdx + .dbf
AdsConnect( "C:\data\app" )
```

### File tables — ADT / ADI / ADM (local)

Same folder path, but select the typed-file format. Tables are `*.adt`
(with `*.adi` indexes and `*.adm` memo stores created automatically when
needed). Supports a richer set of field types (see
[`field-types.md`](field-types.md)).

```harbour
AdsSetServerType( ADS_LOCAL_SERVER )
AdsSetFileType( ADS_ADT )         // .adt typed files
AdsConnect( "C:\data\app" )
```

### SQLite

```
sqlite://<path-to-db-file>[?key=<passphrase>]
```

| Part | Meaning |
|------|---------|
| `<path-to-db-file>` | The single-file database. Created if missing. |
| `?key=<passphrase>` | *(optional)* Open an encrypted database file. |

```
sqlite://./demo.db
sqlite:///var/lib/app/demo.db
sqlite://./secret.db?key=correct horse battery staple
```

Build flag: `OPENADS_WITH_SQLITE` (on by default).

### PostgreSQL

```
postgresql://<user>:<pass>@<host>:<port>/<database>
```

Aliases `postgres://` and `pgsql://` are accepted and normalised to
`postgresql://`.

```
postgresql://app:secret@127.0.0.1:5432/demo
```

Build flag: `OPENADS_WITH_POSTGRESQL`.

### MariaDB / MySQL

```
mariadb://<user>:<pass>@<host>:<port>/<database>
```

Alias `mysql://` is accepted. IPv6 hosts go in brackets:
`mariadb://app:secret@[::1]:3306/demo`.

```
mariadb://app:secret@127.0.0.1:3306/demo
```

Build flag: `OPENADS_WITH_MARIADB`.

### ODBC

Everything after the scheme is handed to the ODBC driver manager
verbatim as a connection string, so any engine with an ODBC driver is
reachable. Both forms are accepted:

```
odbc://<odbc-connection-string>
odbc:<odbc-connection-string>
```

```
odbc://Driver={ODBC Driver 18 for SQL Server};Server=127.0.0.1,1433;Database=demo;UID=app;PWD=secret;Encrypt=optional
odbc://Driver={MariaDB ODBC 3.1 Driver};Server=127.0.0.1;Port=3306;Database=demo;User=app;Password=secret
odbc://DSN=MyPreconfiguredDataSource
```

Build flag: `OPENADS_WITH_ODBC`. Use this scheme to reach SQL engines
that don't have a dedicated native back-end, through their ODBC driver.

### Remote OpenADS server

```
tcp://<host>:<port>/<server-side-path>
tls://<host>:<port>/<server-side-path>      (encrypted transport)
```

The `<server-side-path>` is resolved by the server process, not the
client. Prefer **forward slashes** even on Windows (`C:/app/data`, not
`C:\app\data`).

```
tcp://192.168.0.10:6262/C:/app/data
tls://db.internal:6262/app/data
```

Two server modes (see
[`local-and-remote.md`](local-and-remote.md#path-mapping---data-uri-and---legacy-paths)
for the full table):

| Server flags | URI path means | Table paths |
|--------------|----------------|-------------|
| `--data C:/app/data` (strict) | Must fall under `--data` | Relative, or absolute under the jail |
| `--data C:/Temp --legacy-paths` | Logical connect dir (often `C:/`) | Legacy absolute `C:/Creative.RAM/...` remounts under `C:/Temp` |

```harbour
// Strict: URI carries the real server folder
AdsConnect60( "tcp://127.0.0.1:6262/C:/app/data", ADS_REMOTE_SERVER, ... )

// Legacy ERP: URI keeps the drive the app always used; --data is physical
AdsConnect60( "tcp://127.0.0.1:6262/C:/", ADS_REMOTE_SERVER, ... )
// USE "C:/Creative.RAM/..."  ->  C:/Temp/Creative.RAM/... on the server
```

After connecting, every call (`AdsOpenTable`, navigation, SQL) is
identical to the local path — see
[`local-and-remote.md`](local-and-remote.md).

## Quick reference

| Scheme prefix | Back-end | Runs |
|---------------|----------|------|
| *(folder path)* | DBF/CDX/NTX or ADT/ADI | in-process |
| `sqlite://` | SQLite | in-process |
| `postgresql://` `postgres://` `pgsql://` | PostgreSQL | client→server |
| `mariadb://` `mysql://` | MariaDB / MySQL | client→server |
| `odbc://` `odbc:` | any ODBC engine | client→server |
| `tcp://` `tls://` | remote OpenADS server | client→server |

## Building the connection string at run time

Never hardcode credentials or paths in source. Read them from the
environment (or a config file) and build the URI at run time — this is
exactly what the examples do, e.g.:

```harbour
LOCAL cUri := GetEnv( "DEMO_DB_URI" )      // set per machine, never committed
IF Empty( cUri )
   cUri := "sqlite://./demo.db"            // safe local default
ENDIF
```
