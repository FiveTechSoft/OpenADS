# Remote engine record-lock limits

Remote-owned engine tables retain at most 4096 record-lock entries, including
virtual locks under a table lock and automatic SQL INSERT append locks.
The append check runs before writing a blank row, so rejection cannot leave
an extra record. Re-locking an existing record remains valid; releasing a lock
restores capacity. Exclusive ADT appends that acquire no record lock are exempt.
Local engine tables retain the existing behavior.

This complements the network session's explicit/AppendBlank lock ledger.
It is a per-table cap, not a shared connection-wide counter. Many tables and
SQL statement work still need their own resource/execution budgets.
