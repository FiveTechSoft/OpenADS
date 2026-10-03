# Native SQL dictionary authorization

Authenticated native dictionary SQL enforces operation permissions even when
AdsStmtSetTableRights is left at zero or a caller requests ignoring rights.
Statement flags are not authorization to bypass dictionary grants. SELECT
checks every primary/join source and every inline WHERE/CASE/aggregate FILTER
subquery before materialization. UNION/derived members re-enter the same
boundary. Dictionary aliases and filenames resolve to the protected object.
UPDATE, INSERT and DELETE retain their distinct operation requirements.

No-dictionary and no-ACL behavior stays open. Anonymous local setup and admin
privileges retain their existing behavior. This change is intentional for
named users who previously relied on native SQL ignoring explicit ACLs.
External SQL backends and column-level expression/source enforcement are
separate audit areas; this fix addresses native table operation permissions,
not every column-disclosure path. Protocol and on-disk formats are unchanged.
