# SQL re-entry depth

AdsExecuteSQLDirect accepts at most 8 nested calls on one thread. Procedures,
triggers, embedded script SQL and UNION re-entry share the counter. Excessive
recursion returns an error instead of exhausting the native stack. The counter
unwinds on exit so a failed call does not block later statements. The limit
applies locally and remotely; unusually deep legitimate call chains must be
flattened. Trigger debug traces no longer print raw SQL, which can contain secrets.

This is a call-depth limit, not a query execution deadline. Native DLL routines,
external databases, scans, joins and result memory need separate budgets.
