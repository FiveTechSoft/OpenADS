# ADS-compatible replication (phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One-way asynchronous OpenADS↔OpenADS replication using the Advantage publication / article / subscription model, durable `*.replq` queue, and the 12 `SP_*` names already stubbed in ACE.

**Architecture:** Capture hooks on `Table` mutations enqueue row images into a dedicated append-only queue (not `TxLog`). DD objects store publications/articles/subscriptions. `SP_PROCESSREPLICATIONQUEUES` (and a later `openads_serverd` worker) applies pending LSNs to the subscriber via existing `RemoteConnection` / local table APIs, matching rows by article identity columns.

**Tech Stack:** C++17, CMake, doctest, existing `DataDict` native v2 JSON, `platform::File`, ACE `dispatch_sp_builtin`.

**Spec:** `docs/superpowers/specs/2026-08-12-ads-compatible-replication-design.md`  
**AgentBrain:** `C:\agentbrain\projects\database\openads-engine\ads-replication.md`

---

## File Structure

| Path | Responsibility |
|------|----------------|
| `src/engine/repl_queue.h` / `.cpp` | Durable `*.replq` append / read / LSN |
| `src/engine/repl_catalog.h` / `.cpp` | Fast “is this table published?” + article list |
| `src/engine/repl_capture.h` / `.cpp` | Build queue records from a mutation; best-effort enqueue |
| `src/engine/repl_apply.h` / `.cpp` | Drain queue for one subscription (local or remote) |
| `src/engine/data_dict.h` / `.cpp` | Persist Publication / Article / Subscription |
| `src/engine/table.cpp` / `table.h` | Call capture after append / writeback / delete |
| `src/engine/tx.cpp` / `connection.cpp` | Emit TX_BEGIN / TX_COMMIT / TX_ABORT into the queue |
| `src/abi/ace_exports.cpp` | Replace `unsupported("replication…")` with real `SP_*` |
| `src/network/server.h` / `.cpp` | Optional apply worker thread (after manual drain works) |
| `src/CMakeLists.txt` | Add the four new `.cpp` files to `openads_core` |
| `tests/CMakeLists.txt` | Register new unit tests |
| `tests/unit/repl_queue_test.cpp` | Queue format |
| `tests/unit/repl_catalog_test.cpp` | Catalog + DD round-trip |
| `tests/unit/abi_repl_sp_test.cpp` | `SP_*` without network |
| `tests/unit/abi_repl_apply_test.cpp` | Capture + local apply end-to-end |
| `docs/dd-v2-design.md` | Add the three OBJ_TYPEs; drop “future work” non-goal |

Do **not** invent MariaDB binlog/GTID. Do **not** speak SAP `ADS_REPLICATION_CONNECTION` wire.

---

## Locked APIs

### Queue on-disk record (LE)

```
0-3    magic 0x52504C51  ('R','P','L','Q')
4      type  1=INSERT 2=UPDATE 3=DELETE 4=TX_BEGIN 5=TX_COMMIT 6=TX_ABORT
5      flags bit0=has_before bit1=has_after
6-7    payload length (uint16)
8-15   lsn (uint64)
16-23  tx_id (uint64, 0 = auto-commit)
24-27  crc32c(header[0..23] + payload)   // same Castagnoli as TxLog
28..   payload
```

Row payload (types 1–3): `u16 name_len + utf8 source_table` then `u16 nident` then `nident × (u16 klen + key + u16 vlen + value)` then if has_before `u32 blen + bytes` then if has_after `u32 alen + bytes`.

TX_* payload is empty.

CRC: copy the software CRC-32C from `src/engine/tx_log.cpp` into an anonymous namespace in `repl_queue.cpp` (do not make `tx_log.cpp`’s function public).

### `engine::ReplQueue`

```cpp
enum class ReplRecType : std::uint8_t {
    Insert = 1, Update = 2, Delete = 3,
    TxBegin = 4, TxCommit = 5, TxAbort = 6
};

struct ReplIdent { std::string name; std::string value; };

struct ReplRecord {
    ReplRecType type = ReplRecType::Insert;
    std::uint64_t lsn = 0;
    std::uint64_t tx_id = 0;
    std::string source_table;
    std::vector<ReplIdent> identity;
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
};

class ReplQueue {
public:
    util::Result<void> open(const std::string& path); // create if missing
    util::Result<std::uint64_t> append(const ReplRecord& rec); // assigns lsn
    util::Result<std::vector<ReplRecord>> read_from(std::uint64_t after_lsn);
    std::uint64_t high_water_lsn() const noexcept;
    bool is_open() const noexcept;
};
```

Corrupt tail: `read_from` stops at first bad magic/crc (like `TxLog::read_all`). Do not fail `open` on a trailing corrupt record.

Path: `<dd-stem>.replq` next to the `.add` (e.g. `mydb.add` → `mydb.replq`).

### DD objects

Add to `DataDict` (same pattern as `create_view`):

```cpp
struct PublicationEntry {
    std::string name;
    std::string comment;
    bool enabled = true;
};
struct ArticleEntry {
    std::string name;
    std::string publication;
    std::string source_table;
    std::string target_table;          // empty → same as source
    std::vector<std::string> identity_cols;
    std::string filter;                // stored, not evaluated in phase 1
    bool enabled = true;
};
struct SubscriptionEntry {
    std::string name;
    std::string publication;
    std::string target_uri;
    std::string user;
    std::string password;              // same care as Link.pwd
    std::uint64_t last_lsn = 0;
    bool enabled = true;
};

util::Result<void> create_publication(const PublicationEntry&);
util::Result<void> drop_publication(const std::string& name); // fail if subscriptions remain
util::Result<void> create_article(const ArticleEntry&);      // require identity_cols non-empty; source_table in DD
util::Result<void> drop_article(const std::string& publication, const std::string& name);
util::Result<void> create_subscription(const SubscriptionEntry&);
util::Result<void> drop_subscription(const std::string& name);
util::Result<void> set_subscription_last_lsn(const std::string& name, std::uint64_t lsn);

const std::unordered_map<std::string, PublicationEntry>& publications() const;
// articles keyed "publication::name"
const std::unordered_map<std::string, ArticleEntry>& articles() const;
const std::unordered_map<std::string, SubscriptionEntry>& subscriptions() const;
```

`save()` must emit `OBJ_TYPE` `Publication` / `Article` / `Subscription`. Loader branch next to `View` in `data_dict.cpp`. JSON `fmt:1`.

Duplicate names: error code `5000` with a clear message (same as empty trigger name).

### `engine::ReplCatalog`

```cpp
class ReplCatalog {
public:
    void reload(const DataDict& dd);
    bool table_is_published(const std::string& table_alias) const;
    std::vector<ArticleEntry> articles_for_table(const std::string& table_alias) const;
};
```

Lookup is case-insensitive (fold like other DD maps).

### Capture

```cpp
// Best-effort: never returns a failing Result to the writer.
void repl_capture_row(Connection* c, Table& t, ReplRecType type,
                      const std::vector<std::uint8_t>* before,
                      const std::vector<std::uint8_t>* after);
void repl_capture_tx(Connection* c, ReplRecType type, std::uint64_t tx_id);
std::uint64_t repl_enqueue_failures(); // atomic counter
```

If no DD or table not published: no-op. Queue path from `c->dd_path()` stem + `.replq`. On enqueue failure: increment counter + `util::log` / error log; do not fail the client write.

Identity values: read current field text for each `identity_cols` name from `Table` (same getters the ABI uses). If a column is missing, skip that article and log.

`tx_id`: `c->tx().active() ? c->tx().id() : 0`. Emit `TX_BEGIN` once per active tx on first captured row (flag on `Tx` or `Connection`).

### Apply (phase 1 local first)

`repl_apply_once(DataDict& publisher_dd, const std::string& queue_path, const std::string& subscription_name)`:

1. Load subscription; abort if missing/disabled.
2. `read_from(last_lsn)`.
3. For each record:
   - TX_BEGIN: start grouping
   - row ops: apply to **local** target first in tests (same process, second table path). Remote `RemoteConnection` is Task 7.
   - TX_COMMIT: persist `last_lsn`
   - TX_ABORT: drop group, persist `last_lsn` of the abort record (skip)
   - tx_id==0: apply + persist immediately
4. Apply error: stop, leave `last_lsn`, return the error.

Local apply in tests: subscriber is another directory with the same table file created by the test. `target_uri` may be a filesystem path in phase-1 tests (`file://` or a bare directory). Remote URI (`tcp://`) in Task 7.

Row apply local:
- INSERT: open target table, check identity not present, append, write fields from `after` (if `after` is a raw record image, `write_record_raw` / `AdsSetRecord` equivalent).
- UPDATE: seek identity, write `after`.
- DELETE: seek identity, delete.

Simplest identity seek for tests: scan records comparing identity field strings (N is tiny in unit tests). Do not require an index.

### `SP_*` (replace the unsupported block at `ace_exports.cpp` ~21853)

All require `dd` except `SP_TESTREPLICATIONCONNECTION` (connect only) and `SP_PROCESSREPLICATIONQUEUES` (needs dd + queue).

| uname | args (`arg(i)`) | action |
|-------|-----------------|--------|
| `SP_CREATEPUBLICATION` | 0 name, 1 comment | `create_publication` |
| `SP_DROPPUBLICATION` | 0 name | `drop_publication` |
| `SP_CREATEARTICLE` | 0 pub, 1 article, 2 source, 3 target, 4 identity `;`-sep, 5 filter | `create_article` |
| `SP_DROPARTICLE` | 0 pub, 1 article | `drop_article` |
| `SP_CREATESUBSCRIPTION` | 0 name, 1 pub, 2 target_uri, 3 user, 4 password | `create_subscription` |
| `SP_DROPSUBSCRIPTION` | 0 name | `drop_subscription` |
| `SP_MODIFYPUBLICATIONPROPERTY` | 0 name, 1 prop, 2 value | `COMMENT` / `ENABLED` |
| `SP_MODIFYARTICLEPROPERTY` | 0 pub, 1 article, 2 prop, 3 value | `FILTER` `IDENTITY` `ENABLED` `TARGET` |
| `SP_MODIFYSUBSCRIPTIONPROPERTY` | 0 name, 1 prop, 2 value | `TARGET` `USER` `PASSWORD` `ENABLED` |
| `SP_DELETEREPLICATIONENTRY` | 0 kind, 1 name | kind `PUBLICATION`/`ARTICLE`/`SUBSCRIPTION` (ARTICLE name is `pub::article` or use name only if unique) |
| `SP_GETREPLICATIONENTRYDETAILS` | optional kind, name | **must return true from builtin**; result set via existing `dispatch_sp_builtin_cursor` if that is how other list SPs work — if cursor path is separate, implement there. Minimum: succeed and fill a cursor with columns `KIND,NAME,PARENT,ENABLED,EXTRA`. |
| `SP_PROCESSREPLICATIONQUEUES` | — | `repl_apply_once` for every enabled subscription |
| `SP_TESTREPLICATIONCONNECTION` | 0 uri, 1 user, 2 password | parse `tcp://host:port/path`; `RemoteConnection::connect`; disconnect. Non-tcp uri in tests: if path exists as directory, success. |

No DD → `fail(AE_FUNCTION_NOT_AVAILABLE, "no DD")` (already the pattern).

`AdsDDFindFirstObject` / Next: if those functions already walk DD maps by type code, add cases 19 and 20. If they are still stubs, leave a note in the PR; do not block phase 1 apply on ARC browse.

---

## Tasks

### Task 1: ReplQueue

**Files:**
- Create: `src/engine/repl_queue.h`, `src/engine/repl_queue.cpp`
- Create: `tests/unit/repl_queue_test.cpp`
- Modify: `src/CMakeLists.txt` (add `engine/repl_queue.cpp` after `engine/tx_log.cpp`)
- Modify: `tests/CMakeLists.txt` (add `unit/repl_queue_test.cpp` next to `unit/tx_log_test.cpp`)

- [ ] **Step 1: Write failing tests** in `tests/unit/repl_queue_test.cpp`

```cpp
#include "doctest.h"
#include "engine/repl_queue.h"
#include <filesystem>
namespace fs = std::filesystem;
using openads::engine::ReplQueue;
using openads::engine::ReplRecType;

TEST_CASE("ReplQueue: append INSERT and read back") {
    auto p = fs::temp_directory_path() / "openads_replq.bin";
    fs::remove(p);
    ReplQueue q;
    REQUIRE(q.open(p.string()).has_value());
    openads::engine::ReplRecord r;
    r.type = ReplRecType::Insert;
    r.tx_id = 0;
    r.source_table = "cust";
    r.identity.push_back({"ID", "1"});
    r.after = {1, 2, 3};
    auto lsn = q.append(r);
    REQUIRE(lsn.has_value());
    CHECK(*lsn == 1);
    auto recs = q.read_from(0);
    REQUIRE(recs.has_value());
    REQUIRE(recs->size() == 1);
    CHECK(recs->at(0).source_table == "cust");
    CHECK(recs->at(0).identity[0].value == "1");
    CHECK(recs->at(0).after == std::vector<std::uint8_t>{1,2,3});
    fs::remove(p);
}

TEST_CASE("ReplQueue: read_from skips already applied LSN") {
    // append two records; read_from(1) returns only the second
}

TEST_CASE("ReplQueue: corrupt tail is ignored, prefix survives") {
    // flip last byte; read_from(0) returns the good prefix only
}
```

- [ ] **Step 2: Run the test — expect compile failure** (`repl_queue.h` missing)

```
cmake --build C:\OpenADS\build\default --config Release --target openads_unit_tests -- /v:m /nologo
```

- [ ] **Step 3: Implement `ReplQueue`** mirroring `TxLog` file I/O (`platform::File` write_at / read). Assign `next_lsn_` from the highest good record on open.

- [ ] **Step 4: Run `openads_unit_tests --test-case=ReplQueue*`** — expect PASS.

- [ ] **Step 5: Commit** `feat: add ReplQueue durable replication log`

---

### Task 2: DataDict objects + ReplCatalog

**Files:**
- Modify: `src/engine/data_dict.h`, `src/engine/data_dict.cpp` (maps, create/drop, `save()`, loader)
- Create: `src/engine/repl_catalog.h`, `src/engine/repl_catalog.cpp`
- Create: `tests/unit/repl_catalog_test.cpp`
- Modify: CMake lists as in Task 1
- Modify: `docs/dd-v2-design.md` §2 and §4

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("DataDict: publication/article/subscription round-trip") {
    // DataDict::create(tmp.add)
    // create_publication({name:"P1"})
    // create_article({name:"A1", publication:"P1", source_table:"cust",
    //                 identity_cols:{"ID"}})  // first add_table("cust","cust.dbf")
    // create_subscription({name:"S1", publication:"P1", target_uri:"tcp://127.0.0.1:6262/x"})
    // reopen DataDict::open; CHECK finds the three objects
}

TEST_CASE("ReplCatalog: table_is_published after article") {
    // reload; CHECK catalog.table_is_published("cust")
    // drop_article; reload; CHECK false
}

TEST_CASE("create_article rejects empty identity") {
    // CHECK_FALSE(dd.create_article({... identity empty}))
}

TEST_CASE("create_article rejects free table not in DD") {
}

TEST_CASE("drop_publication fails when a subscription remains") {
}
```

- [ ] **Step 2: Confirm RED** (methods missing).

- [ ] **Step 3: Implement** create/drop + JSON + `save()` `mk("Publication", …)` etc. Loader `else if (obj_type == "Publication")`.

Article key: `publication + "::" + name` (same idea as triggers).

- [ ] **Step 4: Tests PASS.**

- [ ] **Step 5: Commit** `feat: persist ADS publication/article/subscription in DD`

---

### Task 3: Capture hooks

**Files:**
- Create: `src/engine/repl_capture.h`, `src/engine/repl_capture.cpp`
- Modify: `src/engine/table.cpp` (`append_record`, `writeback_record_`, delete)
- Modify: `src/session/connection.cpp` (commit / rollback → `repl_capture_tx`)
- Modify: `src/engine/table.h` if Table needs a back-pointer to `Connection*` (use existing `tid_` / connection hook if one exists; **do not** add a raw pointer cycle if Table already has a way to reach Connection — search `conn_` / `owner_` first). If none, pass a capture callback set by Connection when the table is opened.

**Preferred wiring (avoid cycles):** `Connection::open_table` registers:

```cpp
t->set_repl_sink([this](Table& tbl, ReplRecType ty,
                        const std::vector<uint8_t>* b,
                        const std::vector<uint8_t>* a) {
    repl_capture_row(this, tbl, ty, b, a);
});
```

- [ ] **Step 1: Failing engine/ABI test** in `tests/unit/abi_repl_apply_test.cpp` (capture-only case):

```cpp
TEST_CASE("repl capture: published append enqueues INSERT") {
    // create DD + table cust(ID C 10, NAME C 20) + publication + article identity ID
    // AdsConnect60 to that dir/DD, AdsOpenTable, AdsAppend, set ID/NAME, AdsWriteRecord
    // open ReplQueue next to the .add; read_from(0); one INSERT, identity ID
}

TEST_CASE("repl capture: unpublished table writes nothing") {
}

TEST_CASE("repl capture: enqueue failure does not fail AdsWriteRecord") {
    // optional: chmod directory read-only is flaky on Windows — skip if hard;
    // unit-test ReplQueue append error path separately if needed.
}
```

- [ ] **Step 2: RED** — AdsWriteRecord succeeds but queue empty / file missing.

- [ ] **Step 3: Implement capture + hooks.** `after` for INSERT/UPDATE = current raw record (`read_record_raw` / `record_buf_`). `before` for UPDATE/DELETE = image taken before write (writeback already reads `cur` when tx active; always snapshot for capture).

- [ ] **Step 4: Tests PASS.**

- [ ] **Step 5: Commit** `feat: enqueue published table mutations on ReplQueue`

---

### Task 4: `SP_*` create/drop/modify

**Files:**
- Modify: `src/abi/ace_exports.cpp` (the block at ~21853)
- Create: `tests/unit/abi_repl_sp_test.cpp`

- [ ] **Step 1: Failing tests** using ACE (`AdsConnect60`, `AdsCreateSQLStatement`, `AdsExecuteSQLDirect` / execute procedure — copy the pattern from `abi_dd_proc_view_test.cpp` or `abi_sql_dd_sql_test.cpp`).

```cpp
TEST_CASE("SP_CREATEPUBLICATION fails without DD") { /* AE_FUNCTION_NOT_AVAILABLE */ }
TEST_CASE("SP_CREATEPUBLICATION + ARTICLE + SUBSCRIPTION persist") {}
TEST_CASE("SP_CREATEARTICLE rejects table not in DD") {}
TEST_CASE("SP_DROPPUBLICATION fails if subscription exists") {}
TEST_CASE("SP_MODIFYSUBSCRIPTIONPROPERTY ENABLED") {}
TEST_CASE("SP_TESTREPLICATIONCONNECTION accepts existing directory") {}
```

- [ ] **Step 2: RED** — still `"replication is not supported"`.

- [ ] **Step 3: Replace the unsupported group** with handlers calling `DataDict` methods. Keep `return true` so the dispatcher does not fall through to “procedure not found”.

- [ ] **Step 4: PASS.**

- [ ] **Step 5: Commit** `feat: implement ADS replication stored procedures`

---

### Task 5: Local apply + `SP_PROCESSREPLICATIONQUEUES`

**Files:**
- Create: `src/engine/repl_apply.h`, `src/engine/repl_apply.cpp`
- Modify: `ace_exports.cpp` (`SP_PROCESSREPLICATIONQUEUES`)
- Modify: `tests/unit/abi_repl_apply_test.cpp`

- [ ] **Step 1: Failing integration test**

```cpp
TEST_CASE("repl apply: INSERT/UPDATE/DELETE reach subscriber table") {
    // pub dir + sub dir, same schema cust.dbf
    // publication/article identity ID, subscription target = sub dir
    // write 1 row on publisher, UPDATE, DELETE
    // EXECUTE PROCEDURE SP_PROCESSREPLICATIONQUEUES()
    // open subscriber table; row state matches
}

TEST_CASE("repl apply: BEGIN/COMMIT is all-or-nothing on target") {
    // two inserts in one AdsBeginTransaction; after commit, both on subscriber
}

TEST_CASE("repl apply: stops on identity clash and does not advance last_lsn") {}
```

- [ ] **Step 2: RED** — procedure is no-op or missing apply.

- [ ] **Step 3: Implement `repl_apply_once`** against local filesystem target (subscription `target_uri` is a directory path in these tests). Persist `last_lsn` via `set_subscription_last_lsn` + `save()`.

- [ ] **Step 4: PASS.**

- [ ] **Step 5: Commit** `feat: apply replication queue locally via SP_PROCESSREPLICATIONQUEUES`

---

### Task 6: Transaction markers

**Files:**
- Modify: `src/session/connection.cpp` commit/rollback
- Modify: `tests/unit/abi_repl_apply_test.cpp`

- [ ] Emit `TX_BEGIN` on first captured write of an active tx (not on AdsBeginTransaction with no writes).
- [ ] Emit `TX_COMMIT` / `TX_ABORT` on outer commit / rollback (`tx_nest_depth_ == 0` after decrement).
- [ ] Test: rollback on publisher → subscriber unchanged; `last_lsn` still advances past the abort group.

- [ ] Commit `feat: group replication apply by source transaction`

---

### Task 7: Remote apply + server worker (only after 1–6 are green)

**Files:**
- Modify: `src/engine/repl_apply.cpp` — if `target_uri` starts with `tcp://` or `tls://`, use `RemoteConnection`
- Modify: `src/network/server.h` / `.cpp` — background thread calling `repl_apply_once` on a short interval (e.g. 200 ms) for DDs listed in `openads.ini` `[replication] dictionaries=` **or** any DD opened by a session
- Test: two in-process `Server` instances on ephemeral ports (see `abi_remote_*_test.cpp` / `network_server_test.cpp`)

Skip this task in the first PR if time is tight: phase 1 is complete for apps that call `SP_PROCESSREPLICATIONQUEUES` (ADS also has that pump). Document that the daemon worker is the remaining slice.

- [ ] Commit `feat: push replication apply over RemoteConnection`

---

### Task 8: Docs + AgentBrain close

- [ ] Update `docs/dd-v2-design.md` object table; remove “DD replication … future work” from non-goals (or mark phase 1 done).
- [ ] Short note in `docs/en/whatsnew.md` / `CHANGELOG.md` if that is the project convention.
- [ ] Update AgentBrain `ads-replication.md` status to implemented (which tasks).
- [ ] Session inbox + `validate_session.py --latest <agent>`.

---

## Build / test commands

```
cmake --build C:\OpenADS\build\default --config Release --target openads_unit_tests -- /v:m /nologo
C:\OpenADS\build\default\tests\Release\openads_unit_tests.exe --test-case=ReplQueue*
C:\OpenADS\build\default\tests\Release\openads_unit_tests.exe --test-case=*repl*
```

Do not run the full suite until a task is green; then run `*repl*` plus `data_dict*` / `abi_dd*` if you touched `DataDict::save`.

MSVC incremental: if `ace_exports.cpp` does not rebuild, touch it.

Author for commits: `MiMo V2.5 Free <mimo@opencode.ai>` (OpenADS skill).

---

## Out of this plan

- Two-way / `origin_id`
- `CONFLICT` triggers
- Row filter evaluation
- Initial snapshot
- Studio UI
- SAP ADS wire
- Queue encryption
- Publishing from Local Server without `SP_PROCESSREPLICATIONQUEUES`

---

## Spec coverage

| Spec item | Task |
|-----------|------|
| `*.replq` format + LSN | 1 |
| DD Publication/Article/Subscription | 2 |
| Capture all writes, even auto-commit | 3 |
| Best-effort enqueue | 3 |
| 12 `SP_*` | 4 |
| Apply + `last_lsn` | 5 |
| Tx grouping | 6 |
| `openads_serverd` worker + `tcp://` | 7 (optional first PR) |
| No WAL reuse / no MariaDB | entire plan |
| Tests listed in spec | 1, 3, 5 |

## Placeholder scan

No TBD. `SP_GETREPLICATIONENTRYDETAILS` cursor path is specified as “use existing builtin-cursor dispatcher or succeed with a KIND/NAME/PARENT/ENABLED/EXTRA cursor”. `AdsDDFindFirstObject` is explicitly non-blocking for phase 1.
