# OpenADS v1.09.73

## Security hardening: published fixes and tested boundaries

This release collects the security-hardening changes published since v1.09.72.
The corrections have regression tests; they do not constitute a security
certification or a claim that all vulnerabilities have been eliminated.

### Authentication, authorization and transport

* Native server TLS is available with `OPENADS_WITH_TLS=ON` and both
  `--tls_cert` / `--tls_key`. Configured data listeners require TLS without a
  plain fallback. TLS remains opt-in; plain TCP is still possible without it.
* Safer loopback defaults and explicit opt-in for public anonymous exposure.
* Salted PBKDF2-SHA256 password verifiers, constant-time checks and migration
  of legacy dictionary passwords after a valid login.
* Reconnect-resistant login throttling, authenticated management operations
  and DB:Admin checks for dictionary administration.
* Native SQL, wire and native APIs enforce dictionary operation/column grants
  across the corrected table, expression, schema, row and index/copy paths.
  No-dictionary and anonymous local setup retain compatibility behavior.
* SQL text containing possible secrets is omitted from corrected tracing,
  telemetry and error paths.

### Remote resource limits and explicit errors

* Pre-auth frames, connection/partial-frame deadlines, tables/cursors, mutexes,
  locks and field-count allocations now have defined limits. Remote mutex
  contention fails instead of occupying a worker indefinitely.
* Fetch/FetchWhere reject cells above 65,535 bytes and oversized replies.
  Existing guards now have boundary tests at 65,535, 65,536 and 70,000 bytes.
  Unknown columns and field-read errors fail rather than succeeding as empty.
* Script parser/execution and SQL input/reentry are bounded. Native SELECT,
  joins, predicate subqueries, DML scans, UNION staging and memo reads have
  preflight or size checks.
* One remote native SQL call tree shares 1,000,000 checkpoints and a
  cooperative 5-second deadline. Nested calls do not refill it.
* SQLite remote direct queries use VM progress checks. PostgreSQL and MariaDB
  remote direct queries stream results and check the shared allowance.
  Materialized results are limited to 100,000 rows / 64 MiB conservative
  accounting before application copies. PG/Maria budget/query failures close
  the affected connection; callers must reconnect and discard its old state.
  Optional backend availability depends on the build configuration.

### ZIP validation and safer replacement

* Complete entry names are inspected before path selection; embedded NUL,
  unsafe names and pre-existing symlink escapes are rejected.
* Incomplete output, length and CRC failures no longer report successful
  extraction. Encrypted entries require a password and validate the
  traditional ZipCrypto header before creating output.
* Creation/extraction stage output before publication, preserving an existing
  file on ordinary failed writes. This is per-file replacement, not an
  archive-wide transaction or crash-durability guarantee.
* Remote archive calls limit entries, data and metadata and check a cooperative
  deadline: 100,000 entries, 1 GiB data, 64 MiB metadata, 30 seconds per call.

## Also since v1.09.72

* macOS in-process byte-lock registry and close-time cleanup restore conflict
  checks between handles in the same process. The old expected-failure marks
  on the affected lock tests were removed.
* CHARACTER field widths above 255 read/write the descriptor high byte in
  the merged contributor fix.
* Package staging verifies public headers, ACE aliases and version stamps.

## Known limits and deployment requirements

* Concurrent replacement of archive ancestors/symlinks remains an open race.
  Protect data directories from untrusted filesystem writers.
* Cooperative limits do not provide a universal execution timeout or total
  process-memory/disk quota. Blocking I/O, uninstrumented work, retained cursors
  and other driver methods need separate controls. A driver can allocate a
  giant incoming row before its size is checked.
* Client socket closure is not confirmed server-side query cancellation.
  There is no general rollback guarantee for earlier writes. Do not blindly
  retry failed DML.
* Studio still requires an HTTPS reverse proxy and firewall. Configure TLS
  explicitly for data connections and verify certificates.
* ZipCrypto is weak compatibility encryption, not authenticated encryption.
  Its password check is only 8 bits, so wrong passwords can collide,
  especially for empty files. It cannot guarantee rejection of every wrong
  password and should not protect sensitive backups against attackers.
* macOS binaries are included only after this release's build and test steps
  complete successfully. If validation fails, publication must stop rather
  than label an untested package as validated.
* Windows MinGW static libraries follow the existing lean build (TLS and HTTP
  disabled, tests not run on that sub-build). Do not use them as TLS-enabled
  builds or treat the main MSVC test result as direct static-library coverage.

## What to test

1. Named login, password migration and least-privilege dictionary grants.
2. TLS listener setup and expected rejection of plain connections to it.
3. Client handling of Fetch errors, resource-limit failures and PG/Maria
   reconnects, without automatic retries of writes whose outcome is uncertain.
4. ZIP failures with an existing target file and oversized/unsafe archives.
5. macOS same-process lock contention and CHARACTER widths above 255.

## Validation

Source hardening matrix run56 passed, including live PostgreSQL/MariaDB tests:
https://github.com/FiveTechSoft/OpenADS/actions/runs/37120459771

Binary release build/test results: pending. Do not publish this draft as a
completed validation statement until the release CI results are verified.

## Packages

* `openads-1.09.73-windows-x64.zip`
* `openads-1.09.73-windows-x86.zip`
* `openads-1.09.73-linux-x64.tar.gz`
* `openads-1.09.73-macos-universal.tar.gz` (arm64 + x86_64, subject to validation)

## Resumen en español

Correcciones publicadas y probadas de autenticación, permisos, TLS nativo,
recursos remotos, Fetch, scripts/SQL, backends y archivos ZIP. También incluye
los cambios de locks macOS y campos CHARACTER de más de 255 bytes posteriores
a v1.09.72.

La cobertura sigue siendo parcial: carreras de symlinks/ancestros, cuotas
agregadas, trabajo sin checkpoints, I/O bloqueante y cancelación del servidor
no quedan resueltos universalmente. TLS es opt-in; Studio requiere proxy HTTPS.
ZipCrypto solo verifica 8 bits y admite colisiones de contraseñas incorrectas.
No se afirma "todo OK" ni seguridad completa. Ante un límite/error PG/Maria
se cierra la conexión afectada; hay que reconectar y no reintentar escrituras
a ciegas. Las bibliotecas estáticas MinGW no incluyen TLS/HTTP y se construyen
sin sus propios tests, conforme al perfil existente.
