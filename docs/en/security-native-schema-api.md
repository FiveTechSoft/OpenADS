# Native schema API authorization

Named non-admin dictionary connections can no longer use native CreateTable,
DropTable, RestructureTable, CreateIndex61, PackTable, ZapTable or Reindex on
production tables, even with full DML permission or rights checking disabled.
No-DD and local anonymous compatibility are unchanged. Administrators retain
schema authority.

Internal SQL cursor creation uses a connection-scoped internal grant around
only the exact CreateTable call. A caller-chosen temporary-looking filename
cannot obtain it. Maintenance of registered materialized result handles remains
allowed for ERP result browsing; a live source cursor is not exempt.
This closes these native schema entrypoints, not every native row, index or
copy API. Those and archive password handling remain separate audit surfaces.
