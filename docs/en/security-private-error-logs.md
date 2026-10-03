# Private SQL diagnostics

SQL failures still return full ACE diagnostics to the requesting caller.
Persistent ads_err.dbf and ads_err.log entries retain the SQL error code and
request context, but no SQL source or diagnostic text. Parse errors, embedded
SQL and RAISE messages can quote passwords or private data. Debug traces also
hide AdsConnect101 connection strings, last-error text and remote SQL errors.

This intentionally reduces stored diagnostic detail. Reproduce a failure in a
controlled environment and read AdsGetLastError directly instead of enabling
raw secret logging. Existing logs are not rewritten: rotate or remove them
under your retention policy if earlier builds recorded sensitive statements.
Other subsystem logs, operator-supplied file paths and external backend logging
still need their own access controls and review.
