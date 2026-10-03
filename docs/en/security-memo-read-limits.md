# Remote memo read limits

Native remote-owned tables cap each memo/binary value read at 8 MiB.
FPT checks its declared length before allocating. ADM checks the in-record
length before allocating. DBT checks each block before growing its string.
The checks happen in the store, not after constructing a large value.
Local reads retain their size allowance. All FPT/ADM reads additionally check
the declared payload against the current file size before allocating, so a
forged or truncated file cannot trigger a multi-gigabyte allocation first.

This covers native table reads and SQL paths that read those fields. It is
not a total SQL-result allocator budget: many allowed memo values, expression
results, repeated/derived stages, and external backends still need separate
controls. Oversized remote memo reads return an error; predicates that already
handle field errors as false retain that behavior. Files and protocol layouts
are unchanged. Use local tools for trusted values larger than the remote cap.
