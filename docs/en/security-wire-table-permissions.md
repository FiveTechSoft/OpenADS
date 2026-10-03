# Dictionary permissions on direct wire operations

Named dictionary wire sessions now authorize direct table operations before
engine handlers run. Opening a protected table requires SELECT regardless of
caller rights flags. Alias and filename/path access share an ACL object.
APPEND requires INSERT, field/record updates and locks require UPDATE, and
DELETE requires DELETE. Admin is required for schema changes, maintenance,
raw filesystem functions and archive operations in a named dictionary session.

Intentional compatibility change: direct opens of column-restricted tables are
rejected, because the wire row format carries the whole physical row. Use SQL
with permitted columns instead. Exclusive opens require admin. Cursor handles
without verified source provenance cannot be mutated by non-admin users.
Non-DD sessions retain their existing policy, including the file-function
configuration switch. This is not a complete column or external-backend audit.
