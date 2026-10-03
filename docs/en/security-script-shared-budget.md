# Shared script work budget

All nested Executors on a synchronous thread share one 1,000,000-step budget
and a cooperative five-second deadline. Statements, loop iterations and
expression evaluations consume steps. Creating another executor through a
procedure, trigger or UDF does not reset the budget. Eight executor levels are
allowed. Exhaustion propagates past CATCH; a subsequent independent run starts
with a fresh budget. Local and remote script execution use the same limits.

The clock is checked every 256 steps, not by forcibly interrupting a thread.
Native DLLs, SQL scans and external backends can block inside a bridge call;
this limit cannot cancel them while they run. Side effects completed before
exhaustion are not automatically rolled back. Use a transaction when required.
