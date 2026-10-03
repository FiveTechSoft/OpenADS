# Native SQL column permissions before evaluation

For named dictionary users with column SELECT grants, authorization now checks
column references before reading predicates, sorting, grouping, aggregates,
CASE, scalar/arithmetic expressions and window inputs. Physical filename access
uses the same dictionary object as alias access. Simple SELECT * still projects
only permitted columns. Administrators and unrestricted sources are unchanged.

Intentional compatibility change: joins involving a column-restricted source,
inline subqueries involving such a source or restricted outer row, raw script
expressions, nested scalar calls and complex wildcard projections fail closed
with native error 7079 (public SQL envelope 7200). The current executor has no
reliable column lineage for these shapes; table SELECT permission alone cannot
make their hidden columns safe. Use explicit permitted single-source queries,
or have an administrator review the grants. This does not claim complete
column-security coverage for non-SQL table APIs or external SQL backends.

Simple restricted SQL cursors are also materialized with only permitted fields,
not returned as live-table handles with a presentation-only projection. Raw
record reads, named-field requests and subsequent filters cannot recover fields
omitted from that physical result. Failed materialization does not fall back to
a live restricted source.
