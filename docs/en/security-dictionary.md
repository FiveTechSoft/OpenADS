# Dictionary authentication and administration

New passwords use salted PBKDF2-SHA256 verifiers. Legacy plaintext values are
verified and migrated on a successful login. Named logins always verify the
password, even when anonymous access is allowed. Public property 1101 is now
write-only and returns error 5138 when read.

Dictionary mutations, administrative stored procedures and native DLL
procedure registration require DB:Admin membership. Anonymous local setup
remains available, but never grants remote administration. These are intentional
changes to unsafe earlier behavior. TCP still needs external TLS protection.
SQL text is omitted from client tracing because it can contain passwords.
