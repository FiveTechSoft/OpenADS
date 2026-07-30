## v1.8.40 — RusSoft ERP PR batch + CI unblock

Everything since **v1.8.39**. Integrates open PRs #148–#155 from @russimicro and unblocks the unit-test CI that had been red on every PR.

### Critical / correctness

- **#146 / #150 — `SELECT … ORDER BY` keeps source column types.**  
  The static cursor from #136 rebuilt the schema through `build_memory_result`, typing every numeric as int32 and capping the record at 64 KB. Wide ERP tables failed with ADSCDX/1012; decimals were truncated. The cursor is materialised from the **source** field descriptors again (temp table, own index space, files removed on `AdsCloseTable`).

- **#154 — Partial SEEK keeps `Found()` true when deleted rows are hidden.**  
  Under SET DELETED ON, `Table::seek_key` padded the search key to full index width before re-deriving `exact`. A prefix key (e.g. `CON+DOC` on `CON+DOC+STR(SEQ,6,0)`) landed on the right row but `AdsIsFound` returned 0. Compare only the caller-supplied bytes.

- **#155 — `ordScope` with a bound shorter than the key is a prefix.**  
  Clipper/DBFCDX treat a short scope bound as a prefix. Full-width compare / padding emptied the scope for multi-field tags. Bounds are prefix-compared; `AdsSetScope` no longer pads.

- **#152 — ADT tables named `.DAT` work end to end.**  
  Navigational counterpart to #143. Create no longer forces `.adt`; structural bag follows format (ADI, not CDX); `AdiIndex` companion lookup no longer assumes `.adt`.

- **#151 — Create honours an absolute path outside `data_dir` when its parent exists.**  
  Follow-up to #142 for scratch/TEMP work tables. Parent missing or bare drive-root still folds.

- **#148 — ADI tag ordinals follow creation order (like CDX).**  
  `add_tag` appended instead of prepending. Existing bags keep old ordinals until rebuild.

### Performance

- **#153 — Counting live keys no longer re-reads the table per record.**  
  `AdsGetKeyCount` / scoped record count under SET DELETED ON used `goto_record()` (invalidates read-ahead every row). New `Table::deleted_at(recno)` keeps the block cache warm, then recnos are sorted so file order is sequential. On a 34k-row ADT: ~1.4 s → ~15–25 ms per count (FiveWin TXBrowse calls it several times on open).

### CI

- **`abi_pritpal_lock_test`** — remote URI pointed at non-existent `//Temp`; now uses the staged fixture directory.
- **`abi_create_index_path_test`** — case-insensitive check for auto-appended `.cdx` on Linux/macOS.

### Compatibility notes

- Tables created before **v1.8.39** may still lack field-descriptor displacements (bytes 12–15); recreate if a strict reader reports a corrupt header.
- Index bags with custom extensions (e.g. `.Z01`) written before **v1.8.38** may be NTX-format; re-create with 1.8.38+ client **and** server.
- ADI bags written before **#148** keep reversed ordinals until rebuilt.

### Tests

New / updated unit tests: `abi_sql_orderby_types_test`, `abi_adi_tagdir_order_test`, `abi_adt_dat_extension_test`, `abi_create_outside_datadir_test`, `abi_prefix_seek_deleted_test`, `abi_scope_partial_key_test`, `abi_keycount_deleted_scan_test`, plus CI fixture fixes for pritpal lock and create-index path.
