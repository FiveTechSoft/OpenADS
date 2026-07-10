# adsbackup — OpenADS backup & restore utility

`adsbackup` is a command-line tool for backing up and restoring OpenADS
data — either a **data dictionary** (a `.add` file together with every
table bound to it) or a **directory of free tables**. It is shaped after
the `adsbackup.exe` utility that ships with SAP Advantage Database
Server: the same positional arguments and the same option letters, so
existing backup scripts and scheduled tasks can usually be pointed at
this binary unchanged.

It is built on the same backup engine
(`src/engine/backup.h/.cpp`) that powers the `sp_BackupDatabase`,
`sp_BackupFreeTables`, `sp_RestoreDatabase` and `sp_RestoreFreeTables`
system procedures — one code base for both entry points, so the CLI and
the SQL procedures always behave identically.

Because a backup is easy to add to any scheduler (Windows Task
Scheduler, cron, launchd), this is the recommended way to create
recurring backups.

## Usage

```
backup a data dictionary:    adsbackup [options] <src.add> <dest dir>
backup free tables:          adsbackup [options] <src dir> [file mask] <dest dir>
restore a data dictionary:   adsbackup -r [options] <image.add> <dest.add>
restore free tables:         adsbackup -r [options] <image dir> <dest dir>
```

- **src.add / src dir** — what to back up: the path to the data
  dictionary file, or the directory containing free tables.
- **file mask** — free-table backups only: which files count as tables,
  e.g. `*.adt` or `*.dbf`. Several masks can be combined with
  semicolons: `"*.adt;*.dbf"`. Default: `*.adt;*.dbf`.
- **dest dir / dest.add** — where the backup image goes, or (for a
  dictionary restore) the `.add` path to create. Restoring under a
  different dictionary file name renames the dictionary.

Paths are plain filesystem paths. On Windows both drive letters and UNC
paths (`\\server\share\dir`) work; on Linux and macOS use normal POSIX
paths — the tool makes no drive-letter assumptions.

Companion files travel automatically: each table is copied together
with its index and memo files (`.adi`/`.adm` for ADT tables,
`.cdx`/`.fpt`/`.dbt`/`.ntx` for DBF tables), and a dictionary backup
includes the dictionary's own `.ai`/`.am` sidecars.

## Examples

Back up the `motors.add` dictionary every night (Windows):

```
adsbackup c:\data\motors\motors.add c:\data\motors\backup
```

Test-restore that image into a separate directory (safer than
restoring over the live data):

```
adsbackup -r c:\data\motors\backup\motors.add c:\data\motors\restore\motors.add
```

Back up a directory of free tables on Linux:

```
adsbackup /srv/data "*.dbf" /srv/backup/nightly
```

Restore it, refusing to overwrite anything that already exists:

```
adsbackup -r -d /srv/backup/nightly /srv/data
```

Back up only two tables of a dictionary, metadata included:

```
adsbackup -i customers,invoices c:\data\app.add d:\backup\app
```

Whenever you set up a new backup job, run one restore into a scratch
directory and point your application at it — a backup is only proven
once a restore has been tested.

## Options

| Option | Meaning |
|---|---|
| `-r` | Restore instead of backup (backup is the default). |
| `-i <t1,..,tn>` | Include list — only these tables are processed. For a dictionary, use the table object names; for free tables, the base file name with extension (`table1.adt`). |
| `-e <t1,..,tn>` | Exclude list — these tables are skipped. Same naming rules as `-i`. |
| `-m` | Metadata only: copy just the data dictionary file(s), no tables. |
| `-d` | Don't overwrite existing target files — a warning is reported and the file is skipped. Default is to overwrite. |
| `-p <password>` | Accepted for SAP compatibility. The OpenADS file-level backup needs no credentials; encrypted tables are copied byte-for-byte and stay encrypted in the image. |
| `-v <1-10>` | Lowest severity of warnings/errors to print (default 1). |
| `-a`, `-f` | Differential prepare / differential backup — **not supported**; a full backup is performed and a warning is printed. |
| `-c -h -q -u -w -s -t -n -o` | Accepted and ignored. In SAP's utility these configure the record-level transfer (character set, rights checking, locking, table types, server connection) and the backup log table; the OpenADS file-level backup has no use for them. |

Exit code is `0` on success (warnings allowed below severity 5) and
non-zero when a hard error occurred (severity ≥ 5 or the operation
itself failed).

## The sp_* procedures use the same options

The `Options` string of `sp_BackupDatabase('c:\backup', 'include=t1,t2;MetaOnly')`
et al. accepts the option keywords from the SAP documentation, mapped to
the same core the CLI uses:

| Keyword | Handling |
|---|---|
| `Include=` / `Exclude=` | Honored (comma-separated table lists). |
| `MetaOnly` | Honored — dictionary file(s) only. |
| `DontOverwrite` | Honored (restore). |
| `NoWarnings` | Accepted (OpenADS doesn't emit the table-created info entries this suppresses). |
| `ArchiveFile=` / `ArchiveFileCompressed=` / `ForceArchiveExtract` | Not supported — the image is always written as individual files; a warning row is returned. |
| `Diff` / `PrepareDiff` | Not supported — a full backup is performed; a warning row is returned. |
| `TableTypeMap=` / `User=` / `DDPassword=` | Accepted with a warning row; a file-level backup has no use for them. |

The procedures return the SAP-shaped result set: **an empty result set
means complete success**; otherwise each row carries Severity, Error
Code, Error Message, Table Name and Additional Info for one warning or
error.

## How the backup is made (and its limits)

OpenADS performs a **file-level** backup: the dictionary, tables and
companion files are copied as files. When invoked through the sp_*
procedures, the engine flushes every open table first so the image
matches its in-memory state. This differs from SAP's utility, which
performs a record-level online copy through the server:

- Backing up tables that are being **actively written at the same
  moment** yields a crash-consistent image (equivalent to a power-off
  snapshot), not a transactionally clean one. For a guaranteed-clean
  image, run the backup at a quiet moment or stop writers first.
- Differential backups and single-file (tar/gz) archive images are not
  implemented.
- Tables stored outside the source directory tree are flattened to
  their base name inside the image, with a warning row telling you so.

The image itself is just files — a restore is possible with nothing but
a file copy, and any DBF/ADT tool can open the tables inside it.
