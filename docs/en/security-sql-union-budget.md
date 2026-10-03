# Remote UNION staging budget

Native remote UNION and UNION ALL share an additive allowance across all
members: 100,000 input rows and 64 MiB of fixed-width row data. Each member
is checked before its rows are copied into the UNION accumulator. A rejected
member cursor is closed. Deduplication does not refund the allowance; even
UNION of identical rows can be rejected. Local execution is unchanged.

The bound closes the repeated-member staging bypass of per-source SELECT
preflight. It is conservative and does not cap total allocator overhead,
individual memo sizes, expensive expressions, nested stages or statement
series. These still need their own controls. File and protocol formats do
not change. Use local execution or partition large trusted batch work.
