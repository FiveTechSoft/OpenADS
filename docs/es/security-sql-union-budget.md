# Presupuesto de staging UNION remoto

UNION y UNION ALL nativos remotos comparten un margen aditivo entre todos
sus miembros: 100.000 filas de entrada y 64 MiB de datos de ancho fijo.
Cada miembro se comprueba antes de copiar sus filas al acumulador UNION.
El cursor rechazado se cierra. Eliminar duplicados no devuelve presupuesto:
incluso un UNION de filas iguales puede rechazarse. El SQL local no cambia.

Cierra el bypass de staging por repetir miembros que caben individualmente
en el preflight SELECT. Es conservador y no limita todo el overhead del
allocator, memos individuales, expresiones costosas, etapas anidadas o series
de sentencias. Esos casos necesitan controles propios. Los formatos de
archivo y protocolo no cambian. Use ejecución local o divida batches grandes.
