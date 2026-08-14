## Fixed — `ordListClear()` + `ordListAdd()` left stale index cache causing "SetOrder 5000" / "Workarea not indexed 301" (Pritpal Bedi)

Harbour/Xbase++ ADSCDX clients saw two errors with `INDEX ON … TAG`,
`ordListClear()`, `ordListAdd()`, `SET ORDER TO`, `dbGoBottom()` /
`dbSeek()` against a remote server:

1. **Error 301** ("Workarea not indexed") when the CDX was created in the
   same session and then reopened via `ordListAdd()`.
2. **Error 5000** ("SetOrder") when the CDX was already on disk and
   auto-opened at table open time.

**Root cause**: the client-side remote `AdsCloseAllIndexes` sent the wire
close but never invalidated the cached `index_handles` / `index_by_tag` /
`index_by_name` state in `RemoteTable`. The subsequent `AdsOpenIndex`
dedup check found stale tag entries and skipped pushing the new wire
handle into `index_handles` → `GetIndexHandleByOrder` returned 5000; or
left `index_handles` empty → seek returned 301.

Additionally, `AdsOpenIndex`'s remote path had a premature `break` before
returning the handle array on success, so the caller never received the
opened index handles.

**Fix**:
- `AdsCloseAllIndexes`: clear `index_handles`, `index_by_tag`,
  `index_by_name`, `index_by_order`, `index_by_path`, and
  `index_by_order_map` on successful wire close.
- `AdsOpenIndex`: restructure remote path to return handles on success.

### Binaries

| File | Contents |
|------|----------|
| `openads-1.8.75-windows-x64.zip` | ace64.dll / openace64.dll + openads_serverd.exe |
| `openads-1.8.75-windows-x86.zip` | ace32.dll / openace32.dll + openads_serverd.exe |
| `openads-1.8.75-linux-x64.tar.gz` | libace64 / libopenace64 + serverd |
| `openads-1.8.75-macos-universal.tar.gz` | libace64 / libopenace64 + serverd (arm64+x86_64) |

**Full Changelog**: https://github.com/FiveTechSoft/OpenADS/compare/v1.8.74...v1.8.75
