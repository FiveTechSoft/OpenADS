# Prevalidación del destino DML remoto

UPDATE, DELETE y MERGE nativos remotos comprueban el número físico de registros
y el volumen estimado de filas de ancho fijo antes del lock de escritura DML,
los índices de producción, los predicados o cualquier cambio de registros.
Se aplican los límites de fuentes SELECT: 100.000 registros y 64 MiB.
El handle rechazado se cierra. El SQL local por lotes no cambia.

La comprobación es conservadora: un WHERE selectivo o una condición MERGE no
eluden el límite. Divida el mantenimiento grande o ejecútelo localmente.
No es un plazo ni garantiza rollback. Los memos, expresiones costosas,
sentencias repetidas, fuentes INSERT, backends externos y DLLs nativas aún
necesitan controles propios. Sigue activo el límite del ledger de locks remoto.
