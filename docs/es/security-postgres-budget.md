# Límites de consultas directas PostgreSQL remotas

`exec_sql` y `run_sql` remotos usan libpq no bloqueante y resultados de una fila,
con el plazo y contador compartidos de la llamada SQL remota. Al terminar bien
se restaura el modo anterior. Las consultas locales conservan su ejecución
síncrona. Las listas de comandos devuelven solo el último resultado como
`PQexec`, pero el presupuesto acumula las filas de todos los comandos.

Se rechazan más de 100.000 filas o 64 MiB de contabilidad conservadora de
filas/celdas/campos antes de copiar cadenas, incluidos resultados descartados
por `exec_sql`. Ante límite o error SQL/protocolo se cierra esa conexión: hay
que reconectar y no reutilizar sus transacciones o cursores. No se devuelven
filas parciales como éxito.

No es una cuota dura de memoria: libpq puede asignar una fila o buffer de
entrada excesivo antes de que la aplicación lo inspeccione. No limita el
trabajo ni memoria del servidor. Cerrar el socket no confirma cancelación en
el servidor ni revierte sentencias ya confirmadas. La conexión inicial y otros
métodos PostgreSQL (tablas, navegación, metadatos, agregados y escrituras)
mantienen rutas síncronas. MariaDB, otros drivers, cursores retenidos y bloqueos
externos requieren controles propios. La cobertura es parcial.
