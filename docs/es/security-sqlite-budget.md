# Limites de VM SQLite y resultados materializados

Con un presupuesto SQL remoto activo, run_sql/exec_sql instala un callback cada
1.000 instrucciones de VM y consume el presupuesto compartido. El agotamiento
interrumpe la VM. El limite temporal de valor SQLite de 64 MiB se restaura al
salir y se retira el callback. Los resultados se limitan a 100.000 filas o 64 MiB
contabilizados entre celdas y sobrecoste de filas/vectores, antes de copiar otra
fila. El uso local normal no cambia.

Son limites por consulta e interrupcion cooperativa de VM. No limitan todas las
asignaciones internas, cursores retenidos acumulados, esperas por bloqueo,
llamadas de disco bloqueadas ni drivers externos. Se aplican las transacciones
propias de SQLite; un fallo no implica rollback de todo un script. Siguen
pendientes PostgreSQL/MariaDB y las carreras de ancestros/enlaces. La auditoria
no esta terminada.
