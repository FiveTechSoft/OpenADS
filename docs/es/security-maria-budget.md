# Límites de consultas directas MariaDB remotas

`exec_sql` y `run_sql` remotos usan consultas/fetch por continuaciones de MariaDB
Connector/C y `mysql_use_result`. Las esperas de socket comprueban el plazo y
contador SQL compartidos. El éxito exige consumir hasta EOF antes de liberar;
las filas descartadas por `exec_sql` también consumen presupuesto. Las consultas
locales conservan su ruta anterior.

Se rechazan más de 100.000 filas o 64 MiB de contabilidad conservadora de
celdas/filas/campos antes de copiar. Ante límite o error SQL/protocolo/espera,
se apaga el socket, se evita drenar el resultado pendiente y se cierra esa
conexión. Hay que reconectar y no reutilizar sus transacciones/cursores.
Los resultados múltiples se rechazan cerrando la conexión.

Requiere continuaciones de MariaDB Connector/C, no la API asíncrona distinta de
MySQL. No limita todas las asignaciones: el driver puede asignar una fila enorme
antes de comprobar su longitud. No confirma cancelación en el servidor ni
limita sus recursos ni revierte comandos ya confirmados. Conexión inicial,
tablas/navegación/agregados/escrituras y cursores retenidos quedan pendientes.
La cobertura es parcial.
