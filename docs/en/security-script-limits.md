# Script execution limits

Script programs have a one-million step execution budget. Every statement
and loop iteration consumes it, including empty WHILE bodies. Once exhausted,
execution errors; TRY/CATCH cannot replenish the budget. This stops a trivial
infinite-loop denial of service. It is not a wall-clock deadline for embedded
SQL, DLL calls or slow filesystem operations. SQL values substituted from
script variables use quoted literals and escape single quotes by doubling.
Applications should still bind untrusted values rather than build SQL text.

Parser limits: source 1 MiB, 4096 tokens, 128 nesting levels.
