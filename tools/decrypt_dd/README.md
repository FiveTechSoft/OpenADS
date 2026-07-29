# decrypt_dd — bulk-decrypt every table in an Advantage data dictionary

## Is there a built-in "decrypt all"?

**No.** SAP Advantage has no single call that decrypts a whole database. What it
does have is a per-table operation, which is what this script loops over:

| Mechanism | Level | Notes |
|---|---|---|
| `EXECUTE PROCEDURE sp_DecryptTable('tbl')` | SQL | For a DD-bound table, pass **only** the table name — no password. The engine reads the key from the dictionary. The 2-arg form is for free tables. |
| `AdsDecryptTable(hTable)` | ACE C API | Free tables only. DD tables go through the DD. |
| `AdsDDSetTableProperty(dd,'tbl',214,0)` | ACE C API | `ADS_DD_TABLE_ENCRYPTION = 0` decrypts a DD table. Equivalent to `sp_DecryptTable`. |
| Advantage Data Architect | GUI | Same per-table property, one table at a time. |

So "decrypt everything" = enumerate `system.tables`, then call `sp_DecryptTable`
on each encrypted one. That's what `decrypt_dd.php` does.

## What the engine requires

From SAP's docs (`ace_adsdecrypttable.htm`, `master_sp_decrypttable.htm`,
`ace_adsddsetdatabaseproperty.htm`):

- **ALTER permission** on the tables — connect as `ADSSYS`.
- **Exclusive access** per table. Get everyone off the database first.
- **Not inside a transaction.**
- Table header encryption info is cleared automatically once the table finishes;
  subsequent writes go out unencrypted.
- If `ADS_DD_ENCRYPT_INDEXES` was on, **indexes stay encrypted until the table is
  re-indexed** — use `--reindex`.
- The dictionary's table encryption password can only be cleared once **every**
  table is decrypted; otherwise ADS returns `AE_DD_REQUEST_NOT_COMPLETED`.

This rewrites every record of every table in place. **Back up the data directory
first.**

## Which PHP — this matters

One `php.exe` (`C:\php\php.exe`), two configurations:

| ini | extension | `extension_loaded()` name |
|---|---|---|
| `C:\php\php_sapads.ini` | `php_ads.dll` | `ads` — **SAP ACE** |
| `C:\php\php_openads.ini` | `php_openads.dll` | `openads` — OpenADS |
| `C:\php\php.ini` (CLI default) | both listed | `openads` loses the duplicate-name race and fails to load; you silently get SAP |

**Both extensions register the same class names** (`AdsConnection`,
`AdsDictionary`), so `class_exists()` cannot tell you which engine you are
talking to — only `extension_loaded('ads')` vs `extension_loaded('openads')` can.
The script checks this and refuses to run under the wrong one; override with
`--engine=openads` or `--engine=any`.

Always pass `-c` explicitly rather than relying on the default php.ini.

Decrypting **must** go through SAP: OpenADS currently ships `AdsDecryptTable` as
a stub (see `TODO.parity.md`), so `--engine=sap` is the default for good reason.

## Usage

Dry run — read only, lists what would change:

```
php -c C:\php\php_sapads.ini decrypt_dd.php \
    --path='\\172.16.0.138:6262\e$\adsdata\sfi\mp.add' \
    --user=adssys --password=SECRET
```

Do it:

```
php -c C:\php\php_sapads.ini decrypt_dd.php \
    --path='\\172.16.0.138:6262\e$\adsdata\sfi\mp.add' \
    --user=adssys --password=SECRET --apply --reindex
```

Also strip the dictionary-level encryption settings so new tables aren't
re-encrypted (`ADS_DD_ENCRYPT_NEW_TABLE`, `ADS_DD_ENCRYPT_INDEXES`, and the
table encryption password):

```
... --apply --reindex --disable-dd
```

Other flags: `--only=a,b,c` to restrict to specific tables, `--servertype=<n>`
to override the ACE server type (default remote), `--engine=sap|openads|any`
to change which client extension is required (default `sap`).

Afterwards you can re-run the dry run under OpenADS to confirm it sees the same
tables as unencrypted:

```
php -c C:\php\php_openads.ini decrypt_dd.php --engine=openads \
    --path='...' --user=adssys --password=SECRET
```

Exit codes: `0` clean, `2` connect/catalog failure, `3` one or more tables failed
or are still encrypted.

## Why this script is SQL-only

Do not "simplify" the detection back to `AdsDictionary::getTableProperty()`.

php_ads's `getTableProperty` / `getDatabaseProperty` hardcode the **input** buffer
length to `sizeof(buf) - 1 == 1023` (`F:\php_advantage\src\ads_misc.c`). SAP
validates that length against the property's declared width, so every 2-byte
Boolean property — including `ADS_DD_TABLE_ENCRYPTION` (214) — fails with:

```
5133  The supplied buffer is not the expected size for the specified property.
      The table encryption flag is a 2 byte Boolean value.
```

The **setters** are fine (they pass the real value length); only the getters are
broken. That asymmetry is dangerous: a first version of this script read the flag
via the C API, got 5133 for all 95 tables, concluded "0 encrypted", reported
"all tables report unencrypted", and then happily applied the `--disable-dd`
writes. Detection must not depend on that call.

`system.dictionary` and `system.tables` expose the same fields over SQL with no
buffer involved:

| Instead of | Use |
|---|---|
| `getTableProperty(t, 214)` | `SELECT Name, Table_Encryption FROM system.tables` |
| `getDatabaseProperty(105/106/119/120)` | `SELECT * FROM system.dictionary` |
| `setDatabaseProperty(...)` | `EXECUTE PROCEDURE sp_ModifyDatabase(prop, value)` |
| `setTableProperty(t, 214, ...)` | `EXECUTE PROCEDURE sp_ModifyTableProperty(t,'TABLE_ENCRYPTION','FALSE',NULL,NULL)` |

If you ever want to fix the extension itself, the getters need to pass the
property's expected size in `usLen` rather than the full buffer size.

## Notes

- The script never prints the table encryption password, only whether one is set
  (`system.dictionary.Encrypt_Table_Password` is readable on an admin connection).
- `sp_DecryptTable` on an already-plain table returns error 5163
  (`AE_TABLE_NOT_ENCRYPTED`); that is treated as success.
- `--disable-dd` clears `ENCRYPT_TABLE_PASSWORD` **first**, because doing so forces
  `ENCRYPT_NEW_TABLE` off as a documented side effect. If that clear fails (error
  5130 — encrypted tables still present) the other flags are left untouched, so a
  failed run can't leave the dictionary half-configured.
- After `--disable-dd`, `sp_SetDDEncryptionType` becomes usable if you ever want
  to switch the dictionary between RC4 / AES128 / AES256. Be aware that
  `sp_SetDDEncryptionType` **discards all user passwords** — they must be reset
  afterwards.
