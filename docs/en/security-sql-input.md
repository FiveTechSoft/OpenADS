# Remote SQL input bounds

Remote ExecuteSQL rejects embedded NUL instead of executing a silently truncated
prefix. Source is limited to 1 MiB, 4096 lexical units and 128 parenthesis levels.
The same shape check applies when a remote-owned ABI connection re-enters SQL
through a stored procedure or trigger. Strings, quoted identifiers and comments
are scanned without treating their contents as nesting or operator tokens.

These are resource checks, not an SQL injection firewall. Authorized applications
can still execute SQL and must bind untrusted values. The checks do not bound
recursive SQL procedure depth, left-deep expression trees, table cardinality,
joins, result allocation, slow I/O, or database execution wall-clock time.
Those execution paths require separate budgets.
