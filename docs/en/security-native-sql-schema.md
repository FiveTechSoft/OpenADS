# Native dictionary SQL schema authorization

SQL DROP TABLE/INDEX, ALTER TABLE and CREATE TABLE/INDEX/DATABASE now require
the dictionary administrator role when a live dictionary is attached. A table
SELECT or even full DML grant does not authorize deleting the physical file,
changing its schema or rebuilding its production indexes. The check happens
before source reads and filesystem changes; caller rights flags do not bypass it.
Public SQL reports envelope 7200 with native access-denied 7079.

This intentionally changes named non-admin dictionary SQL schema access. Local
anonymous administration and no-dictionary operation retain their compatibility
policy. Session #temp DROP remains private to the connection. SQL CREATE TABLE
AS SELECT is also an admin operation. Internal cursor materialization is not
SQL schema authority and is unchanged. Native non-SQL DDL APIs and direct wire
file/table operations remain separate audit surfaces.
