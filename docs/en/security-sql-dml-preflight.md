# Remote DML target preflight

Native remote UPDATE, DELETE and MERGE now check the physical target count
and estimated fixed-width row volume before acquiring the DML write lock,
binding production indexes, evaluating predicates or changing records.
The existing SELECT source limits apply: 100,000 records and 64 MiB.
A rejected target handle is closed. Local batch SQL is unchanged.

This is deliberately conservative: a selective WHERE or MERGE condition does
not bypass the limit. Partition large maintenance work or run it locally.
This is not a deadline or rollback guarantee. Memo values, expensive expressions,
repeated statements, INSERT sources, external backends and native DLL calls
still require separate controls. The remote lock-ledger cap remains in force.
