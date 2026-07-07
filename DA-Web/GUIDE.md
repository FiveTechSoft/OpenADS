# DA-Web User Guide

DA-Web is a browser-based administration tool for **OpenADS** data dictionaries and free-table directories. It replaces SAP Data Architect with a modern web UI: browse and edit table structure, manage users and permissions, view stored procedures, execute SQL, and generate DDL scripts — all without installing any desktop software.

---

## Table of Contents

1. [What DA-Web Is (and Is Not)](#1-what-da-web-is-and-is-not)
2. [The Interface](#2-the-interface)
3. [Getting Started](#3-getting-started)
   - [Adding a Data Dictionary](#adding-a-data-dictionary-existing-add-file)
   - [Adding a Free-Table Directory](#adding-a-free-table-directory)
   - [Creating a Brand-New Data Dictionary](#creating-a-brand-new-data-dictionary)
   - [Importing a SAP Data Dictionary](#importing-a-sap-data-dictionary)
4. [The Navigation Tree](#4-the-navigation-tree)
5. [Browsing Table Data](#5-browsing-table-data)
6. [Table Structure](#6-table-structure)
   - [Fields](#61-fields)
   - [Indexes](#62-indexes)
   - [Triggers](#63-triggers)
   - [Generate SQL](#64-generate-sql)
7. [Stored Procedures and Functions](#7-stored-procedures-and-functions)
8. [The SQL Editor](#8-the-sql-editor)
9. [Users](#9-users)
10. [Groups and Permissions](#10-groups-and-permissions)
11. [Referential Integrity (RI) Objects](#11-referential-integrity-ri-objects)
12. [Views](#12-views)
13. [Free Tables (No Data Dictionary)](#13-free-tables-no-data-dictionary)
14. [Saving and Sessions](#14-saving-and-sessions)
15. [Keyboard Shortcuts](#15-keyboard-shortcuts)
16. [Configuration Reference](#16-configuration-reference)

---

## 1. What DA-Web Is (and Is Not)

**DA-Web is a Data Dictionary management tool.**

A data dictionary (DD) in ADS/OpenADS is a `.add` file that stores metadata about your database: table definitions, indexes, triggers, stored procedures, users, groups, referential integrity rules, and more. The actual table data lives in `.adt` or `.dbf` files; the DD provides the schema and business rules that govern how that data behaves.

DA-Web lets you:
- Inspect and edit everything stored in a DD
- Browse and edit table rows
- Write and execute SQL against any connected DD
- Create DDL scripts to reproduce your schema
- Manage user accounts and access permissions

**DA-Web is not a general-purpose database client.** It is specifically designed for OpenADS (and SAP-compatible ACE) data dictionaries. It does not connect to MySQL, PostgreSQL, or other RDBMS systems.

**All enforcement happens on the server.** Triggers fire, permissions are checked, and referential integrity is enforced by the OpenADS engine (`openace64.dll`) for every client — including DA-Web. DA-Web is an administration tool, not the enforcer.

---

## 2. The Interface

DA-Web has three main areas:

```
┌─────────────────────────────────────────────────────────────────────┐
│  Menu bar: File  Connection  Tools  SQL  Window  Help               │
├──────────────────┬──────────────────────────────────────────────────┤
│                  │  [Tab 1: leases] [Tab 2: leases Triggers] [+]    │
│   Navigation     │ ─────────────────────────────────────────────── │
│   Tree           │                                                  │
│                  │         Tab Content Area                         │
│  ▶ Pmsys-TestData│   (data grid, metadata grid, SQL editor,        │
│    ▶ Tables      │    trigger panel, RI form, etc.)                 │
│      ▶ leases    │                                                  │
│        Fields    │                                                  │
│        Indexes   │                                                  │
│        Triggers  │                                                  │
│        Generate  │                                                  │
│    ▶ Stored Procs│                                                  │
│    ▶ Users       │                                                  │
│    ▶ Groups      │                                                  │
└──────────────────┴──────────────────────────────────────────────────┘
│ Status bar                                                          │
└─────────────────────────────────────────────────────────────────────┘
```

**Menu bar** — Six menus:

| Menu | Contents |
|------|----------|
| **File** | New SQL Tab, Exit |
| **Connection** | New DD…, Open DD…, Free Tables…, then one entry per registered DD (● = connected, click to disconnect; ○ = disconnected, click to connect), Refresh Tree |
| **Tools** | Refresh Tree |
| **SQL** | Open SQL Editor |
| **Window** | List of currently open tabs; click an entry to bring that tab to the front |
| **Help** | About DA-Web |

**Navigation tree (left pane)** — Lazy-loading hierarchy of everything in your connected DDs. Click to select; click the arrow to expand. Drag the divider to resize.

**Tab content area (right pane)** — Opens a new tab for each object you interact with. Multiple tabs can be open at once. Close a tab with the × button.

**Status bar** — Short messages about the last action (connected, SQL result count, errors).

---

## 3. Getting Started

### First launch

Open your browser and navigate to the DA-Web URL (e.g. `http://localhost/`). The tree pane will be empty because no dictionaries are registered yet.

### Adding a Data Dictionary (existing .add file)

Click **Connection → Open DD…**. Fill in:

| Field | Description |
|-------|-------------|
| **Name** | Label you choose — shown in the tree |
| **Path to .add file** | Full path to the `.add` file |
| **Default username** | ADS username; leave blank to be asked at connect time |
| **Connection type** | `Local` (in-process DLL) or `Remote` (TCP server) |

Click **Add**. The DD appears in the tree and is saved to `config/dictionaries.json`.

**Sample — pmsys:**

| Field | Value |
|-------|-------|
| Name | `Pmsys-TestData` |
| Path | `f:\OpenADS\testdata\pmsys\pmsys.add` |
| Default username | `AdsSysAdmin` |
| Connection type | Local |

### Adding a Free-Table Directory

Click **Connection → Free Tables…**. Fill in:

| Field | Description |
|-------|-------------|
| **Name** | Label you choose — shown in the tree |
| **Directory path** | Full path to the folder containing `.adt` or `.dbf` files |
| **Connection type** | `Local` or `Remote` |

Click **Add**. The directory appears in the tree as a free-table entry.

**Sample — Aquarium:**

| Field | Value |
|-------|-------|
| Name | `Aquarium` |
| Directory path | `f:\OpenADS\testdata\Aquarium\` |
| Connection type | Local |

### Creating a Brand-New Data Dictionary

Click **Connection → New DD…**. Fill in:

| Field | Description |
|-------|-------------|
| **Name** | Label you choose |
| **Path for new .add file** | Full path where the `.add` file should be created (must not exist yet) |
| **Admin password** | Optional encryption/admin password; leave blank for no encryption |
| **Connection type** | `Local` or `Remote` |

Click **Create**. DA-Web calls `ads_dd_create()` in the PHP extension to create the file on disk, then registers it in `config/dictionaries.json` with username `AdsSysAdmin`.

> **Note:** This feature requires that the `ads_dd_create` function is compiled into the `php_openads` extension. If it is not available, DA-Web returns a 501 error with instructions on what to add to the extension.

### Connecting

Click a dictionary name in the tree. If it is not yet connected, a **Connect** dialog appears:

- **Username** — pre-filled with the registered default username
- **Password** — enter the password; leave blank if the DD has none
- **Server host / Port** *(Remote only)* — where to dial `openads_serverd`. Leave both blank to use the server default (`127.0.0.1` and the port set in `api/common.php`'s `API_OPENADS_REMOTE_PORT`). Fill them in to connect to a different host or a non-default port; the values you use are remembered in `config/dictionaries.json` for next time.

After connecting, the tree node expands to show categories.

#### Smart path parsing for remote connections

When adding or editing a DD (New DD, Open DD, Free Tables, Properties), pasting a path in the admin-share form `\\host:port\c$\rest\of\path` — for example `\\localhost:16262\c$\Pmsys\data\pmsys_openads.add` — is recognised automatically: the dialog switches to **Remote**, fills in **Host**/**Port** from the string, and rewrites the Path field to the plain server-side path (`c:\Pmsys\data\pmsys_openads.add`). A plain UNC path with no `:port` (`\\fileserver\share\...`) is left alone, since that's an ordinary network path rather than a request to dial a specific `openads_serverd` instance. The same parsing runs server-side too, so it also works if you call the API directly.

Alternatively, open the **Connection** menu — each registered DD appears with a status indicator:
- **●** (filled dot) — connected; click to disconnect
- **○** (open dot) — disconnected; click to open the Connect dialog

### Removing or editing a registered dictionary

There is currently no UI dialog to remove or rename a registered DD. Edit `config/dictionaries.json` directly to remove or rename entries, then click **Tools → Refresh Tree** (or **Refresh Tree** at the bottom of the Connection menu) to reload the tree.

### Importing a SAP Data Dictionary

If you are migrating from SAP Advantage Database Server, you will have an existing `.add` file created by the SAP tools. DA-Web does not open SAP-format `.add` files directly. You first **import** the SAP DD into an OpenADS DD, and then connect to the new file.

#### Why we import rather than connect directly

The SAP `.add` file uses a closed proprietary binary format with per-user encrypted permission fields that OpenADS cannot fully decode, and with no published write specification. Attempting to write back to the SAP file risks corrupting it. Beyond that, the SAP engine (`ace64.dll`) and the OpenADS engine (`openace64.dll`) have separate in-process connection pools — they cannot both hold a DD open at the same time without risking deadlock or data corruption.

The import approach is clean and safe:

- The import tool reads the SAP `.add` using the SAP ACE DLL (read-only, the original is never changed).
- It writes everything to a new OpenADS `.add` file using the OpenADS engine.
- After import, the SAP DLL is no longer needed. DA-Web connects to the new file through the OpenADS engine alone.
- Your actual data files (`.adt` / `.dbf`) are standard xBase files that both engines read identically — they are never imported, just re-pointed to from the new DD.

This also means that once imported, your application no longer requires a SAP ACE license.

#### What the import preserves

| DD Object | Result |
|-----------|--------|
| Tables (names and paths) | Fully imported |
| Index file registrations | Fully imported |
| Triggers (name, timing, event, body) | Fully imported |
| Stored procedures and functions | Fully imported |
| Users and groups | Fully imported |
| Group memberships | Fully imported |
| Referential integrity rules | Fully imported |
| Group-level permissions | Partially — unencrypted bits are preserved |
| Per-user direct permissions | Not imported — encrypted in the SAP format; re-enter manually after import |

#### How to import

1. Click **Connection → Import SAP DD…** in the menu bar.
2. Fill in the dialog:

   | Field | Description |
   |-------|-------------|
   | **SAP .add file path** | Full path to the existing SAP-format `.add` file |
   | **New OpenADS .add path** | Full path where the new OpenADS DD file should be written (must not exist yet) |
   | **SAP username** | Username for the SAP DD (usually `AdsSysAdmin`) |
   | **SAP password** | Password for the SAP DD |

3. Click **Import**. The status bar updates as tables, triggers, procedures, users, and RI rules are transferred.
4. When the **Done** button becomes active, the import is complete.
5. The new `.add` is automatically registered in `config/dictionaries.json` and appears in the tree.
6. Click the new DD name to connect to it and verify the import.

> **Note:** The import requires that both `php_advantage` (SAP ACE extension) and `php_openads` (OpenADS extension) are loaded in the same PHP instance. The import script calls into `php_advantage` to read from the SAP file and into `php_openads` to write the OpenADS file. If `php_advantage` is not available, the import menu item is hidden.

---

## 4. The Navigation Tree

The tree is lazy-loaded — children are fetched only when you expand a node.

### Data Dictionary nodes

A connected DD expands into these categories:

| Category | Contents |
|----------|----------|
| **Tables** | All tables registered in the DD |
| **Views** | SQL views |
| **Stored Procedures** | Stored procedures (SQL bodies) |
| **Functions** | User-defined functions |
| **Users** | User accounts |
| **Groups** | User groups |
| **RI Objects** | Referential integrity rules |
| **Links** | External DD links |

> **Note:** Triggers appear under each individual table, not as a top-level category. This reflects how ADS stores triggers — each trigger belongs to a specific table.

### Table sub-nodes

Expanding a table reveals four leaves:

```
▶ leases
    Fields          — field names, types, constraints
    Indexes         — index tags and expressions
    Triggers        — trigger bodies and metadata
    Generate SQL    — full DDL script for the table
```

Click any leaf to open it in a new tab.

### Free-table directory nodes

When a free-table directory is connected (no `.add` DD file), the tree lists all `.adt` and `.dbf` files in that directory. Each table expands to:

```
▶ ANIMALS
    Fields          — field list from the DBF/ADT file header
    Indexes         — index tags from the .cdx/.ntx/.adi file
```

Triggers and Generate SQL are not available for free tables because there is no DD to store that metadata.

---

## 5. Browsing Table Data

Click a table name (not a sub-node) to open a data tab. The tab shows up to 2,000 rows in a scrollable grid.

### Seek toolbar

At the top of every data tab is the **seek toolbar**:

```
[ Index dropdown ▼ ]  [ Seek to…     ]  [ Go ]
```

- **Index dropdown** — lists "Natural Order" (no sorting) plus every index tag on the table. Selecting an index and clicking Go reloads the rows sorted by that index.
- **Seek field** — type a value and click **Go** (or press **Enter**) to scroll to the first row where the index field value is ≥ your input. Works for any data type as a string prefix match.
- The dropdown updates automatically when you click a column header to sort.

**Example** — To jump to lease `L0042` in the leases table:
1. Select `LEASEID` from the index dropdown.
2. Type `L0042` in the seek field.
3. Click **Go**. The grid reloads sorted by leaseid and scrolls to `L0042`.

### Navigation buttons

A navigation bar appears inside the pagination area at the bottom:

| Button | Action |
|--------|--------|
| ⟳ | Refresh — reload from server (preserves current ordering) |
| ⤒ | First record |
| ⤓ | Last record |
| ▲ | Previous record |
| ▼ | Next record |
| ＋ | Add new row (enters edit mode) |
| ✕ | Delete selected row |
| ✔ | Confirm pending insert (enabled after adding a row) |

### Editing rows

- **Update** — double-click any cell to edit it. Press **Enter** or click away to commit. Changes are saved immediately to the DD.
- **Insert** — click **＋** to add a blank row at the top. Fill in the fields and click **✔** to insert. The row is refreshed from the server to show any auto-assigned values (AutoIncrement, timestamps, default values).
- **Delete** — click a row to select it, then click **✕**. A confirmation prompt appears before deletion.

---

## 6. Table Structure

### 6.1 Fields

**Path:** expand a table → click **Fields**

The Fields tab shows one row per field with these columns:

| Column | Description | Editable |
|--------|-------------|----------|
| # | Column order number | No |
| Field | Field name | Yes |
| Type | Base data type (Character, Integer, Money, Date, …) | Yes (dropdown) |
| Size | Storage length in bytes | Yes |
| Dec | Decimal places | Yes |
| Required | Yes = field cannot be null | Yes (dropdown: Yes/No) |
| Default | Default value expression | Yes |
| Index | Whether this field is indexed (No / Yes / Primary) | Yes (dropdown) |

**Available types:** Character, CICharacter (case-insensitive), Varchar, Memo, Integer, ShortInt, AutoIncrement, Numeric, Float, Double, Money, Logical, Date, DateTime, Timestamp, Blob, Binary.

**Saving changes:**

Click **Save Changes** to write your edits back to the DD. Currently, Save writes:
- **Required** — updates `Field_Can_Be_Null` in the DD
- **Default** — updates `Field_Default_Value` in the DD

Type and Size changes are displayed but not yet persisted through Save (use `ALTER TABLE MODIFY COLUMN` in the SQL editor for structural changes).

**Reading Required correctly:**

In the DD, `Field_Can_Be_Null = False` means the field **is required** (must have a value). DA-Web translates this: `Required = Yes` means the field is required (cannot be null).

### 6.2 Indexes

**Path:** expand a table → click **Indexes**

Each row represents one index tag:

| Column | Description | Editable |
|--------|-------------|----------|
| Tag | Index tag name | No (drop + recreate to rename) |
| Expression | Field expression (e.g. `leaseid` or `propertyID;EndDate`) | Yes |
| Descending | Sort direction | Yes (Yes/No) |
| Unique | Whether duplicate keys are rejected | Yes (Yes/No) |
| Binary | Case-sensitive key comparison | Yes (Yes/No) |
| Key Type | Character or Numeric (informational) | No |

**Saving index changes:**

Select the row(s) you want to save and click **Save Changes**. For each selected row, DA-Web:
1. Issues `DROP INDEX tag ON table` to remove the old tag
2. Calls `sp_CreateIndex90` to recreate it with the new settings

**Warning:** Dropping and recreating an index on a large table can take time. The table remains accessible during re-indexing but index-guided queries may be slower until it completes.

**Multi-field indexes:** Enter multiple field names separated by semicolons in the Expression column (e.g. `propertyID;EndDate`).

### 6.3 Triggers

**Path:** expand a table → click **Triggers**

The Triggers tab has two panels:

**Top panel — Trigger metadata grid:**

Each row is one trigger with these editable columns:

| Column | Values | Meaning |
|--------|--------|---------|
| Name | (read-only) | Trigger identifier |
| Timing | BEFORE / INSTEAD OF / AFTER | When the trigger fires relative to the DML |
| Event | INSERT / UPDATE / DELETE | Which DML statement fires it |
| Enabled | Yes / No | Whether the trigger is active |
| Priority | number | Execution order when multiple triggers exist on the same event |

Click a row to load its SQL body into the editor below.

**Bottom panel — SQL editor:**

Shows the complete trigger body (read from the `.add/.am` binary directly). The editor is fully editable — modify the SQL here.

**Trigger types explained:**
- `BEFORE INSERT` — fires before a row is inserted; can inspect `__new` (the row being inserted). The actual insert still happens unless the trigger raises an error.
- `INSTEAD OF INSERT` — replaces the normal insert. The trigger SQL must perform the insert itself (e.g. `INSERT INTO table SELECT * FROM __new`). Used when you need custom logic around every insert.
- `AFTER UPDATE` — fires after a row is updated; `__old` contains the original values, `__new` contains the new values.

**Saving trigger changes:**

Click **Save Changes** to write:
- Timing, Event, Enabled — updated in the DD metadata
- SQL body — stored back into the DD container

**pmsys.add leases example:**

```
Insert AuditLog  |  INSTEAD OF  |  INSERT  |  Enabled: Yes
Update AuditLog  |  BEFORE      |  UPDATE  |  Enabled: Yes
```

The `Insert AuditLog` trigger intercepts every insert, logs it to the audit table, then manually performs `INSERT INTO leases SELECT * FROM __new`. The `Update AuditLog` trigger logs every update before it commits.

### 6.4 Generate SQL

**Path:** expand a table → click **Generate SQL**

Opens a SQL editor tab pre-filled with a complete DDL script for the table, including:

1. `CREATE TABLE` statement with all field definitions
2. `EXECUTE PROCEDURE sp_CreateIndex90(...)` calls for every index tag
3. `EXECUTE PROCEDURE sp_ModifyTableProperty(...)` for primary key, default index, permission level
4. `EXECUTE PROCEDURE sp_ModifyFieldProperty(...)` for Required and Default on every field
5. `CREATE TRIGGER [...]` blocks with the full trigger body

The script is compatible with SAP ACE syntax and can be executed in any ACE-compatible SQL tool to recreate the table from scratch.

---

## 7. Stored Procedures and Functions

**Path:** click any procedure under **Stored Procedures** or any function under **Functions**

A split-panel editor opens:

**Left/top — SQL body editor:**
- Full ACE-syntax SQL body, editable
- **Execute** button (or **F5**) runs the body in the currently selected database

**Right/bottom — Parameters grid:**

| Column | Description |
|--------|-------------|
| Name | Parameter name |
| Type | Input or Return (functions only) |
| DataType | ADS data type |
| Size | For character types |
| Decimals | For numeric types |

Use **+ Add** and **− Delete** to manage parameters. Click **Save to DD** to write both the body and parameters back to the data dictionary.

**Functions** additionally have a Return type entry in the parameters grid.

---

## 8. The SQL Editor

Open a new SQL editor tab by clicking **+** in the tab bar or **File → New SQL tab**.

### Selecting a database

Use the **— database —** dropdown at the top of the editor to choose which connected DD to run your SQL against.

### Writing SQL

The editor has full syntax highlighting for ADS/ACE SQL. Type your query directly.

```sql
-- Example: find all active leases expiring this year
SELECT leaseid, TenantName, EndDate, Rent
FROM leases
WHERE inactive = .F.
  AND YEAR(EndDate) = YEAR(NOW())
ORDER BY EndDate
```

### Executing SQL

| Action | How |
|--------|-----|
| Run all | **F5** or **F9** or click **▶ Execute** |
| Run selection | Select text then **Ctrl+Enter** |

Results appear below the editor in a grid. Multi-line cell values are displayed in a wider textarea cell.

### Saving and opening scripts

- **Save** (💾) — saves the current editor content to `config/sql_scripts.json` under a name you provide
- **Open** (📂) — opens a saved script from the list

Saved scripts persist across browser sessions and are shared among all users of the same DA-Web instance.

### Results grid

After a successful `SELECT`, the results grid shows:
- Column headers are sortable by clicking
- Row count is displayed in the status area
- Multiline values (e.g. Memo fields) get an expanded cell with a scrollbar

For `INSERT`, `UPDATE`, `DELETE`, or DDL statements, the result shows the affected row count or a success message. Errors are shown in red.

---

## 9. Users

**Path:** click any user name under **Users**

Opens a **Group Memberships** tab showing every group the user belongs to.

### Adding a user to a group

1. Click **+ Add Group** — a new row appears with a dropdown listing all groups this user is **not** already a member of.
2. Select the group from the dropdown.
3. Click **Save Changes**.

DA-Web calls `sp_AddUserToGroup` for each newly added group.

### Removing a user from a group

1. Click the row you want to remove to select it.
2. Click **− Remove Selected**.
3. Click **Save Changes**.

DA-Web calls `sp_RemoveUserFromGroup` for each removed group.

---

## 10. Groups and Permissions

**Path:** click any group name under **Groups**

Opens a **Permissions** tab showing every DD object the group has rights on.

### The permissions grid

| Column | Meaning |
|--------|---------|
| Object | Table, view, procedure, or database name |
| Type | Object type (Table, Procedure, View, …) |
| Select | Can read rows |
| Insert | Can add rows |
| Update | Can modify rows |
| Delete | Can remove rows |
| Execute | Can call this procedure/function |

All Yes/No cells are editable. Click **Save Changes** to apply your changes.

DA-Web translates your Yes/No selections into SQL `GRANT` and `REVOKE` statements:
```sql
GRANT SELECT, UPDATE ON leases TO Managers
REVOKE DELETE ON leases FROM Managers
```

### How OpenADS enforces permissions

When a client (any client, not just DA-Web) connects with a specific user account, the engine checks that user's group memberships and the group's permissions before allowing any operation. An unauthorised access attempt returns an error — the data is never exposed.

---

## 11. Referential Integrity (RI) Objects

**Path:** click any RI object name under **RI Objects**

Opens a form with the following fields:

| Field | Description |
|-------|-------------|
| **Parent Table** | The table that holds the primary key (the "one" side) |
| **Primary Key Tag** | The index tag on the parent table whose expression is the key |
| **Child Table** | The table that holds the foreign key (the "many" side) |
| **Foreign Key Tag** | The index tag on the child table that matches the parent key |
| **Update Rule** | What happens when a parent key is updated |
| **Delete Rule** | What happens when a parent row is deleted |

**Rules:**
- `Restrict` — reject the change if child rows reference it
- `Cascade` — propagate the change to all child rows
- `SetNull` — set the foreign key fields to null in child rows

Tag dropdowns populate automatically when you select a table.

### Saving RI changes

Click **Save RI**. DA-Web drops the existing RI object (if it already exists) and recreates it with `sp_CreateReferentialIntegrity`.

### Deleting an RI object

Click **Delete RI**. A confirmation prompt appears. DA-Web calls `sp_DropReferentialIntegrity`.

### Creating a new RI object

RI objects appear in the tree once created. To create one:
1. Open the SQL editor
2. Run:
```sql
EXECUTE PROCEDURE sp_CreateReferentialIntegrity(
   'Tenants2Leases',       -- RI name
   'fail_ri',              -- fail table (receives error info)
   'tenants',              -- parent table
   'leases',               -- child table
   'TENANTID',             -- parent key tag
   1,                      -- update rule: 1=Restrict 2=Cascade 3=SetNull
   1                       -- delete rule
);
```
3. Refresh the tree — the new RI object appears under **RI Objects**.

---

## 12. Views

**Path:** click any view name under **Views**

Opens the view's SQL definition in the SQL editor. The definition is read-only for display; to alter a view, modify the SQL and execute `CREATE OR REPLACE VIEW` manually.

---

## 13. Free Tables (No Data Dictionary)

A **free-table directory** is a folder containing `.adt` or `.dbf` table files with no `.add` data dictionary file. This is common for legacy DBF databases or for tables created directly without a DD.

### What works without a DD

| Feature | Available |
|---------|-----------|
| Browse table data | ✓ |
| Index seek and ordering | ✓ (reads index files directly) |
| View fields | ✓ (from binary file header) |
| View index tags | ✓ |
| Edit index tags | ✓ (saves to .cdx/.adi file) |
| Row insert / update / delete | ✓ |
| SQL editor | ✓ |
| Triggers | ✗ (no DD to store them) |
| Generate SQL | ✗ |
| Users / Groups / Permissions | ✗ |
| Stored Procedures | ✗ |
| RI Objects | ✗ |

### Field types from binary header

When no DD is present, field metadata is read directly from the `.dbf` or `.adt` file header. The Required, Default, and Index columns will be blank (those properties are stored in the DD, not in the file header).

---

## 14. Saving and Sessions

### Connection state

Connections are held in your **PHP session** (a server-side file). If you close your browser and reopen it, your session may still be active and connections may still appear as open. If the session expires (typically 24 hours), you will need to reconnect.

### What is saved where

| Data | Stored in |
|------|-----------|
| Registered DDs | `config/dictionaries.json` (local-only, not committed) |
| Saved SQL scripts | `config/sql_scripts.json` |
| Current connections | PHP session (server memory) |
| Field/index/trigger changes | Written to the `.add` DD file |
| Table row changes | Written to the `.adt`/`.dbf` data file |

### Session after Apache restart

If Apache is restarted, PHP sessions are preserved (they live in the filesystem), but active database connections are dropped. The tree will still show your DDs as connected, but clicking into tables will fail. Simply click the DD name to reconnect.

---

## 15. Keyboard Shortcuts

| Shortcut | Context | Action |
|----------|---------|--------|
| **F5** or **F9** | SQL editor | Execute all SQL |
| **Ctrl+Enter** | SQL editor | Execute selected text only |
| **Enter** | Seek input | Trigger Go (same as clicking Go) |
| **Double-click** | Table data cell | Begin editing |
| **Enter** | Cell in edit mode | Commit edit |
| **Escape** | Cell in edit mode | Cancel edit |
| **Click column header** | Data grid | Sort by that column (client-side) |

---

## 16. Configuration Reference

### config/dictionaries.json

Stores the list of registered DDs for one DA-Web installation. This file is local machine state and is intentionally not committed to the repository. DA-Web creates it automatically when you add, create, or import a dictionary. New installs can copy `config/dictionaries.example.json` as a starting point, or just start with an empty list.

Edited via the Manage Dictionaries dialog, but can also be edited manually:

```json
[
  {
    "name": "Pmsys-TestData",
    "path": "f:\\openads\\testdata\\pmsys\\pmsys.add",
    "username": "adssys",
    "connType": "local",
    "entryType": "dd"
  },
  {
    "name": "Aquarium",
    "path": "f:\\openads\\testdata\\Aquarium\\",
    "username": "",
    "connType": "local",
    "entryType": "free"
  },
  {
    "name": "Pmsys-Remote",
    "path": "c:\\Pmsys\\data\\pmsys_openads.add",
    "username": "adssys",
    "connType": "remote",
    "entryType": "dd",
    "host": "192.168.1.50",
    "port": 16262
  }
]
```

| Field | Values | Description |
|-------|--------|-------------|
| `name` | any string | Display label in the tree |
| `path` | file or directory path | `.add` file for DD; directory for free tables |
| `username` | string or `""` | ADS username; blank = no authentication |
| `connType` | `"local"` / `"remote"` | In-process or TCP server connection |
| `entryType` | `"dd"` / `"free"` | Data Dictionary or free-table directory |
| `host` | string *(optional)* | Remote-only. `openads_serverd` host; omitted = server default (`127.0.0.1`) |
| `port` | number *(optional)* | Remote-only. `openads_serverd` port; omitted = server default (`API_OPENADS_REMOTE_PORT` in `api/common.php`) |

`host`/`port` are written automatically the first time you connect successfully with an override (see [Connecting](#connecting)) and are only present on remote entries that use a non-default host or port.

### config/sql_scripts.json

Stores saved SQL scripts. Each entry has a `name` and `sql` field. Managed through the Open/Save buttons in the SQL editor.

### PHP session settings

Sessions use PHP's default file-based storage. Adjust `session.gc_maxlifetime` in `php.ini` if you need longer idle timeouts.

---

## Appendix: ADS Data Types Quick Reference

| Type | Description | Notes |
|------|-------------|-------|
| Character | Fixed-width ASCII string | Max 65,530 bytes |
| CICharacter | Case-insensitive fixed-width string | Same as Character but index comparisons ignore case |
| Varchar | Variable-length string | Max 65,530 bytes; stores only as many bytes as needed |
| Memo | Variable-length text | Stored in companion `.adm` file; no length limit |
| Integer | 32-bit signed integer | Range ±2,147,483,648 |
| ShortInt | 16-bit signed integer | Range ±32,767 |
| AutoIncrement | Auto-incrementing integer | Read-only after insert |
| Numeric | Exact decimal number | Up to 20 digits |
| Money | Currency value (4 decimal places) | 64-bit fixed-point |
| Double | 64-bit IEEE floating-point | |
| Float | Floating-point (variable precision) | |
| Logical | Boolean True/False | Stored as `.T.` / `.F.` |
| Date | Calendar date | YYYY-MM-DD |
| DateTime | Date and time | |
| Timestamp | Unix timestamp with milliseconds | |
| Blob | Binary large object | Stored in companion `.adm` file |
| Binary | Fixed-length binary data | |
