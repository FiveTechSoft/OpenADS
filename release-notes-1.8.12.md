## Changes

### ENGINE — multi-record Skip counts visible rows under SET DELETED ON / filters (M12.33)

- **Fixes the "deleted record duplicates the previous item" browse symptom** introduced when 1.8.10/1.8.11 activated the deleted-row filter on the server: `Table::skip` computed multi-record skips as `recno + delta` (physical arithmetic), landing one *visible* row short for every deleted/filtered row inside the range. The client's prefetch resync (`Skip(step + consumed)`) then re-served a row already painted — the duplicate — or tripped the same-recno EOF heuristic and truncated the walk early on tables longer than the 64-row lookahead.
- `Table::skip` on the natural-order path now walks one record at a time and counts only visible rows, matching the index-order path and Clipper `SKIP n` semantics.
- Also fixes LOCAL `dbSkip(n > 1)` over deleted/filtered rows (latent for years; unreachable remotely before 1.8.10).

Requires updated **openads_serverd** (the engine runs server-side); update **openace64.dll** too for LOCAL mode.

Full details in [CHANGELOG.md](CHANGELOG.md).
