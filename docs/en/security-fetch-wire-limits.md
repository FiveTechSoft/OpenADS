# Fetch wire boundaries and field-read errors

The Fetch/FetchWhere protocol represents each cell with a u16 byte length.
Main already rejects values over 65,535 bytes and applies a 16 MiB response
budget (introduced by 58a357a2). End-to-end tests now cover 65,535, 65,536 and
70,000-byte memo values in base-table Fetch/FetchWhere and SQL-cursor Fetch.
The maximum representable value is returned intact; oversized values fail.

The new correction rejects field-read errors and unknown columns instead of
returning a successful empty cell. SQL-cursor column names which would be
truncated by the 64-byte lookup buffer fail rather than looking up a different
name. Existing size guards are retained. No wire format or successful cell
encoding changes. Broader SQL execution and filesystem race review remain open.
