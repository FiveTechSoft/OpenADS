# Native row, index and copy authorization

Named dictionary users are checked against the actual table path and effective
SELECT/INSERT/UPDATE/DELETE permission on native row API calls. Caller aliases,
rights=0 and open-mode checks do not grant missing operations. Column grants
apply to field reads and writes; whole-row reads, CRC and copies fail closed
when columns are restricted. Named dictionary file-copy and index-mutation
operations require admin. Copy paths must stay under the configured data roots.

Registered materialized SQL results remain private working tables. A live
source cursor is not a materialized result. No-DD and local anonymous operation
retain compatibility. SQL preflight remains responsible for internal engine
reads. This is a bounded API fix, not a claim that the security audit is complete.
