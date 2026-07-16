# OpenADS native Data Dictionary format (`.add`, `OADD`)

Status: **v1** (2026-07-15). This is a first-class, OpenADS-owned format. Unlike
the SAP ADS `.add` binary (which OpenADS could only partially reverse-engineer),
**everything here is specified**, so a reader never guesses — it reads exactly
what a writer wrote.

## Why it exists

The migration tool `import_dd` reads a SAP dictionary through the SAP ACE runtime
(which decodes SAP's format completely and correctly) and writes an OpenADS
dictionary in *this* format. OpenADS then reads and writes only this format. The
SAP-binary reader is therefore an import-time concern, never a runtime one.

## Design goals

- **Self-describing.** Every section carries its column names inline; every value
  carries its length. No fixed offsets, no property blobs, no encryption.
- **Forward compatible.** Each section and each value is length-prefixed, so a
  reader can skip an unknown section or ignore an extra column without failing.
- **Catalog-shaped.** The dictionary is stored as a set of tables — one section
  per `system.*` catalog — so `SELECT * FROM system.X` projects a section
  directly, and a native DD is byte-inspectable against SAP's catalog output.
- **String values.** Every value is stored as UTF-8 text, matching the CICHAR
  columns the ADS `system.*` catalogs expose (`Table_Type` = `"3"`,
  `Index_Options` = `"2051"`, etc.). Consumers that need a number parse it.

## Encoding primitives

All integers are **little-endian**. All text is **UTF-8**.

| Primitive | Layout |
|---|---|
| `u16` | 2 bytes LE |
| `u32` | 4 bytes LE |
| `str` | `u32 length` followed by `length` UTF-8 bytes (no terminator) |
| `sstr` | `u16 length` followed by `length` UTF-8 bytes (for names) |

## File layout

```
Header (12 bytes):
  0..3    magic         = "OADD"  (0x4F 0x41 0x44 0x44)
  4..5    u16 version   = 1
  6..7    u16 flags     = 0       (reserved; readers must ignore unknown bits)
  8..11   u32 nsections

Then `nsections` sections, in order:
  Section:
    sstr  name                     section name, lower-case (e.g. "indexes")
    u32   body_len                 number of bytes in `body` (enables skip)
    body (exactly body_len bytes):
      u32  ncols
      ncols x sstr  column_name    the section's column schema, in order
      u32  nrows
      nrows x ( ncols x str value ) row-major values, one str per column
```

A reader that does not recognise a section name reads `body_len` and skips the
body. A reader that finds extra columns beyond the ones it maps ignores them; a
reader that finds a column missing treats it as empty. This is how the format
evolves without a hard version break.

## Sections (v1)

One section per catalog. Column names are **exactly** the ADS `system.*` column
names (SAP casing), so the `system.*` projection is a pass-through.

| Section | Mirrors | Key columns (not exhaustive) |
|---|---|---|
| `dbprops` | database properties | `Key`, `Value` |
| `tables` | `system.tables` | `Name`, `Table_Relative_Path`, `Table_Type`, `Table_Primary_Key`, `Table_Default_Index`, `Table_Validation_Expr`, ... |
| `columns` | `system.columns` | `Parent`, `Field_Name`, `Field_Type`, `Field_Length`, `Field_Decimal`, ... |
| `indexes` | `system.indexes` | `Name`, `Parent`, `Index_File_Name`, `Index_Expression`, `Index_Condition`, `Index_Options`, `Index_Key_Length`, `Index_Collation`, `Comment` |
| `relations` | `system.relations` | `Name`, `Parent`, `Child`, `Parent_Tag`, `Child_Tag`, `Update_Option`, `Delete_Option`, ... |
| `triggers` | `system.triggers` | `Name`, `Parent`, `Trigger_Type`, `Event`, `Container`, `Procedure`, ... |
| `procedures` | `system.storedprocedures` | `Name`, `Procedure_Input`, `Procedure_Output`, `Procedure_Body`, ... |
| `functions` | `system.functions` | `Name`, `Return_Type`, `Function_Input`, `Function_Body` |
| `views` | `system.views` | `Name`, `View_Definition` |
| `users` | `system.users` | `Name`, ... |
| `usergroups` | `system.usergroups` | `Name`, ... |
| `usergroupmembers` | `system.usergroupmembers` | `Group_Name`, `User_Name` |
| `permissions` | `system.permissions` | `Grantee_Name`, `Object_Name`, `Object_Type`, `Permissions` (bitmask, text) |

The runtime `DataDict` maps a subset of these columns into its typed model for
navigation and enforcement (table paths, index files/expressions, RI rules,
permission bitmasks, trigger bindings); the full column set is preserved for the
catalog projection. Nothing is lost on round-trip: `save()` writes back every
section it read plus any the runtime modified.

## Integrity

The header magic + version identify the format. `DataDict::open()` sniffs the
first 4 bytes:

- `OADD` → native reader.
- **anything else → OpenADS does NOT attempt to read it.** It returns a clear
  "this dictionary must be imported" error (`AE_SAP_PERMS_NEED_IMPORT` / 5174)
  with instructions to run `import_dd`. OpenADS never parses a SAP `.add` at
  runtime; SAP-format reading lives only inside `import_dd`, which uses the SAP
  ACE runtime to do it.

There is no checksum in v1; the file is produced by trusted tooling and
validated structurally on load (every length must stay within the enclosing
bound, or the load fails cleanly).

## Maintenance

This document is the source of truth for the format and is kept in lock-step
with `src/engine/dd_native.{h,cpp}`. Any change to the on-disk layout bumps the
`u16 version`, is described here, and — because sections and values are
length-prefixed — old readers keep working by skipping what they do not know.
