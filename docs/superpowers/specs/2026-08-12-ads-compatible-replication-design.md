# ADS-compatible replication — design

Date: 2026-08-12
Status: approved (brainstorming), pending implementation plan
Canonical AgentBrain copy: `C:\agentbrain\projects\database\openads-engine\ads-replication.md`

## Problem

SAP / Sybase Advantage Database Server ships **Advantage Replication**
(since ADS 8.0, separately licensed): an asynchronous server-to-server
push of table changes through publications, articles, subscriptions,
and a durable queue. Client apps and ARC drive it with
`EXECUTE PROCEDURE SP_CREATEPUBLICATION` / `SP_CREATEARTICLE` /
`SP_CREATESUBSCRIPTION` and related `SP_*`.

OpenADS already knows those names and the ACE constants
(`ADS_REPLICATION_CONNECTION`, `ADS_DD_PUBLICATION_OBJECT`,
`ADS_DD_SUBSCRIPTION_OBJECT`) but every call returns
`AE_FUNCTION_NOT_AVAILABLE` with
`"replication is not supported by OpenADS"`. The data-dictionary v2
design lists replication as future work.

Apps that used ADS replication cannot fail over to OpenADS. A
MariaDB-style binlog/GTID would not help them: ARC and the `SP_*`
surface expect the ADS model.

## Goals

- Phase 1: **one-way**, **async push**, **Data Dictionary required**,
  **OpenADS ↔ OpenADS**.
- Same object model as ADS: Publication / Article / Subscription.
- Same stored-procedure names currently stubbed in
  `src/abi/ace_exports.cpp`.
- Capture every INSERT / UPDATE / DELETE on a published table, with or
  without an explicit transaction.
- Durable queue that survives crash; subscriber tracks last applied
  LSN.
- Apply on `openads_serverd` only (not Local Server / in-process DLL).
- Identify target rows by article identity columns (ADS does **not**
  require a primary key).
- Preserve source transaction grouping on the target.

## Non-goals (phase 1)

- Two-way / multi-master (loop prevention via `origin_id` is phase 2).
- `CONFLICT` triggers on the target (phase 2).
- Column (vertical) filters — ADS v1 did not have them either.
- Replicating to SQL views.
- Wire compatibility with a live SAP ADS
  (`ADS_REPLICATION_CONNECTION` proprietary protocol).
- Queue encryption.
- Publishing from Advantage Local Server / `ace64.dll` without
  `openads_serverd`.
- Using `TxLog` (WAL) as the replication stream.
- MariaDB / PostgreSQL / SQL Server as a replication partner.

## Chosen approach — “ADS objects + capture hook + `*.replq` + apply worker”

Three options were considered:

| | Approach | Verdict |
|---|---|---|
| **A** | ADS API + semantics, OpenADS ↔ OpenADS | **Chosen** |
| B | Also speak SAP ADS replication wire | Rejected for phase 1 (months of reverse engineering) |
| C | MariaDB binlog / GTID | Rejected (breaks ARC and `SP_*`) |

MariaDB knowledge is used only as engineering analogy (async apply
thread, per-subscriber position). The on-disk and ABI shapes are ADS.

## Architecture

```
App  --ACE/SQL-->  openads_serverd (publisher DD)
                       |
                       | Table::append / writeback / delete
                       |   if table is an enabled Article
                       v
                    <dd>.replq     (append-only, monotonic LSN)
                       |
                       | ReplApply worker (continuous)
                       | RemoteConnection tcp:// or tls://
                       v
                   openads_serverd (subscriber DD)
                       | identity-column match
                       v
                   INSERT / UPDATE / DELETE  (grouped by source tx_id)
```

### New components

**`engine::ReplCatalog`** — `src/engine/repl_catalog.{h,cpp}`

In-memory view of Publication / Article / Subscription loaded from
the DD. Fast “is this table published?” lookup on the write path.
Invalidated when any `SP_CREATE*` / `SP_DROP*` / `SP_MODIFY*` mutates
the DD.

**`engine::ReplQueue`** — `src/engine/repl_queue.{h,cpp}`

Append-only file `<dictionary-stem>.replq` next to the `.add`. Not
the WAL (`TxLog`): the WAL only records in-transaction writes and is
truncated after recovery.

**`engine::ReplCapture`** — hook called from `Table` mutation paths.

**`network::ReplApply`** — worker owned by `Server`. Wakes on enqueue
or a short poll. Connects with existing `RemoteConnection`. Applies
pending records whose LSN > subscription `last_lsn`.

**`dispatch_sp_builtin` replication branch** — replace the current
`unsupported(...)` block with real handlers.

### Why not reuse `TxLog`

`src/engine/table.cpp` `writeback_record_()` only calls
`tx_->note_before_image()` when `tx_ && tx_->active()`. Auto-commit
writes never reach the WAL. `TxLog::truncate()` after recovery would
erase unreplicated changes. A dedicated queue is required.

## Data model

### Data Dictionary objects (native v2)

Extend `docs/dd-v2-design.md` §4. Unknown `OBJ_TYPE` values are
already skipped on load (forward compatible).

| OBJ_TYPE | OBJ_NAME | OBJ_KEY | PARENT | JSON |
|----------|----------|---------|--------|------|
| `Publication` | name | — | 0 | `{fmt:1, comment, enabled}` |
| `Article` | article name | publication name | publication OBJ_ID | `{fmt:1, source_table, target_table, identity_cols:[], filter:"", enabled}` |
| `Subscription` | name | publication name | publication OBJ_ID | `{fmt:1, target_uri, user, last_lsn, enabled}` |

`target_uri` is an OpenADS connect string
(`tcp://host:port/logical` or `tls://…`). Password for the apply
connection is stored in the server config / credential store for that
user, not in the JSON, unless ARC requires a password argument on
`SP_CREATESUBSCRIPTION` — then persist it with the same care as DD
`Link` passwords (already `{path, user, pwd}`).

`identity_cols` is an ordered list of field names used to find the
row on the target. Empty list is rejected at article-create time
(phase 1 requires an explicit identity; ADS allowed choosing columns).

`filter` is stored in phase 1 but **not evaluated**. Applying filters
is phase 3.

### Queue record (`*.replq`)

Little-endian, one record:

```
0-3    magic 0x52504C51  ("RPLQ")
4      type  1=INSERT 2=UPDATE 3=DELETE 4=TX_BEGIN 5=TX_COMMIT 6=TX_ABORT
5      flags (bit0 = has_before, bit1 = has_after)
6-7    payload length
8-15   lsn        uint64
16-23  tx_id      uint64   (0 = auto-commit singleton)
24-27  crc32c of header+payload
28..   payload
```

Payload (type 1–3):

```
source_table  u16-len + utf8
identity      u16 count, then repeating (u16-len name, u16-len value)
before        u32-len + raw record bytes   (UPDATE/DELETE)
after         u32-len + raw record bytes   (INSERT/UPDATE)
```

LSN is monotonic per queue file. Never reused.

## Capture

Hook points (all local table mutations, including remote-session
mutations that execute inside `openads_serverd`):

- `Table::append_record` — INSERT
- `Table::writeback_record_` — UPDATE (and delete-flag writes that
  keep the slot)
- Physical / flagged DELETE path used by `AdsDeleteRecord`

Algorithm:

1. If the connection has no DD, return (nothing to capture).
2. Resolve table alias / relative path against `ReplCatalog`.
3. If no enabled article references this table, return.
4. For each matching article, append one queue record.
5. If a transaction is active, emit `TX_BEGIN` once on first captured
   write of that `tx_id`, and `TX_COMMIT` / `TX_ABORT` from the
   existing commit/rollback path.
6. Auto-commit writes get `tx_id = 0` and are applied as a single-row
   transaction on the target.

Capture must not fail the client write if the queue fsync fails:
log to the server error log and increment a counter
(`repl_enqueue_failures`). A full disk that cannot enqueue is
recorded; phase 1 does not block the writer (ADS FAQ: “very little
performance degradation”; queue write is the only synch cost we
accept — best-effort enqueue + error log is enough for v1; document
that operators must monitor the counter).

**Local Server / DLL-only:** if `ReplCatalog` has subscriptions but
there is no `Server` apply worker, enqueue still happens so a later
`SP_PROCESSREPLICATIONQUEUES` on a daemon that mounts the same DD
can drain the queue. `SP_PROCESSREPLICATIONQUEUES` is the manual
pump for that case. Automatic background apply runs only inside
`openads_serverd`.

## Apply

`ReplApply` (one thread per `Server` instance):

1. Load every enabled `Subscription` from every open DD under the
   server data roots (phase 1: DDs already opened by at least one
   connection; plus DDs listed in `openads.ini` `[replication]`
   `dictionaries=`).
2. For each subscription, open `RemoteConnection` to `target_uri`.
3. Read queue records with `lsn > last_lsn`.
4. Group consecutive records that share a non-zero `tx_id` until
   `TX_COMMIT`. On `TX_ABORT`, skip the group.
5. On the target, `AdsBeginTransaction` … apply rows … `AdsCommit`.
6. Persist `last_lsn` on the Subscription object after each committed
   group (or after each auto-commit row).
7. On apply error: leave `last_lsn` unchanged, write
   `repl_apply_error` to the error log, retry with backoff. Do not
   skip (phase 1 is stop-and-retry, not skip). `CONFLICT` handling
   is phase 2.

Row apply:

- **INSERT:** append on `target_table`. If identity already exists,
  treat as apply error (phase 1).
- **UPDATE:** seek by identity columns; write `after` image. Missing
  row → apply error.
- **DELETE:** seek by identity; delete. Missing row → apply error.

Identity seek uses existing table seek / filter APIs. Values come
from the queue payload, not from recno (recno is not stable across
servers).

## Public surface (phase 1)

Names match the current unsupported list. Argument order is the
OpenADS contract below. If ARC is later shown to pass a different
SAP order, adjust the dispatcher and keep these names.

| Procedure | Args | Effect |
|-----------|------|--------|
| `SP_CREATEPUBLICATION` | name, [comment] | DD object `Publication` |
| `SP_DROPPUBLICATION` | name | Drop publication and its articles; subscriptions that reference it error |
| `SP_CREATEARTICLE` | publication, article, source_table, [target_table], identity_cols (`;`-separated), [filter] | DD object `Article`. `source_table` must exist in the DD. |
| `SP_DROPARTICLE` | publication, article | Drop article |
| `SP_CREATESUBSCRIPTION` | name, publication, target_uri, [user], [password] | DD object `Subscription`. Does **not** copy existing rows (snapshot is phase 3). |
| `SP_DROPSUBSCRIPTION` | name | Drop subscription |
| `SP_MODIFYPUBLICATIONPROPERTY` | name, property, value | `COMMENT`, `ENABLED` |
| `SP_MODIFYARTICLEPROPERTY` | publication, article, property, value | `FILTER`, `IDENTITY`, `ENABLED`, `TARGET` |
| `SP_MODIFYSUBSCRIPTIONPROPERTY` | name, property, value | `TARGET`, `USER`, `PASSWORD`, `ENABLED` |
| `SP_DELETEREPLICATIONENTRY` | kind, name | kind = `PUBLICATION` / `ARTICLE` / `SUBSCRIPTION` |
| `SP_GETREPLICATIONENTRYDETAILS` | [kind], [name] | Result set: kind, name, parent, enabled, extra |
| `SP_PROCESSREPLICATIONQUEUES` | — | Drain queue once (blocking, used by tests and DLL-only) |
| `SP_TESTREPLICATIONCONNECTION` | target_uri, [user], [password] | Connect and disconnect; success or `AE_*` |

Without a DD on the connection: same error as other `SP_*` that
require a dictionary (`AE_FUNCTION_NOT_AVAILABLE`, `"no DD"`).

`AdsDDFindFirstObject` / `AdsDDFindNextObject` must enumerate
`ADS_DD_PUBLICATION_OBJECT` (19) and `ADS_DD_SUBSCRIPTION_OBJECT`
(20) once the objects exist.

## Error handling

| Situation | Behaviour |
|-----------|-----------|
| `SP_*` with no DD | `AE_FUNCTION_NOT_AVAILABLE`, `"no DD"` |
| Duplicate publication / article / subscription name | existing DD duplicate error |
| Article on a free table (not in DD) | error, do not create |
| Target unreachable | apply retries; `SP_TESTREPLICATIONCONNECTION` fails immediately |
| Apply identity miss / clash | stop that subscription, log, retry same LSN |
| Queue write failure | client write still succeeds; error log + counter |
| Local Server with subscriptions | queue fills; apply only via `SP_PROCESSREPLICATIONQUEUES` or a later daemon |

## Testing

C++ unit tests (no live network where avoidable):

- Queue append / read / crc / truncate-at-corrupt-tail.
- Catalog “is table published?” after create/drop article.
- Capture hook: mock table write produces a queue record only when
  published.

Integration (two in-process servers or two `openads_serverd` on
ephemeral ports):

1. Create DD + table + publication + article (identity = PK-like
   column) + subscription.
2. INSERT / UPDATE / DELETE on publisher.
3. `SP_PROCESSREPLICATIONQUEUES` or wait for worker.
4. Assert subscriber table matches.
5. Multi-row `BEGIN` / `COMMIT` applies as one target transaction
   (all or none).
6. No DD → `SP_CREATEPUBLICATION` fails.
7. Unpublished table writes produce zero queue records.

## Files to touch (expected)

| File | Change |
|------|--------|
| `src/engine/data_dict.{h,cpp}` | load/save Publication, Article, Subscription |
| `src/engine/repl_catalog.{h,cpp}` | new |
| `src/engine/repl_queue.{h,cpp}` | new |
| `src/engine/repl_capture.{h,cpp}` | new |
| `src/engine/table.cpp` | capture hooks |
| `src/engine/tx.cpp` | TX_BEGIN/COMMIT/ABORT into queue |
| `src/network/repl_apply.{h,cpp}` | new |
| `src/network/server.{h,cpp}` | start/stop apply worker |
| `src/abi/ace_exports.cpp` | implement the 12 `SP_*` |
| `docs/dd-v2-design.md` | add the three OBJ_TYPEs |
| `tests/unit/repl_*.cpp` | new |

## Phase 2 / 3 (not in the first plan)

- **2:** two-way with `origin_id` on queue records so applied rows
  are not re-queued; `CONFLICT` trigger event on the subscriber.
- **3:** evaluate article `filter`; initial snapshot on
  `SP_CREATESUBSCRIPTION`; Studio panel; optional queue encryption.

## Spec self-review

- No TBD placeholders.
- Phase 1 scope is one implementation plan (catalog + queue +
  capture + apply + `SP_*` + tests).
- WAL is explicitly not the stream (no contradiction with “reuse
  existing TxLog”).
- SAP wire interop is explicitly out.
- `SP_*` argument order is defined as the OpenADS contract, with a
  documented escape hatch if ARC proves a different SAP order.
