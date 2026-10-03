# Preflight de trabajo para SELECT remoto

Los sources de SELECT SQL remotos admiten hasta 100.000 registros físicos y
64 MiB estimados de imágenes de fila de ancho fijo. Los joins comprueban
además el producto cartesiano máximo de los recuentos y la suma de anchos
antes de construir hashes o materializar resultados. Un source vacío cuenta
como uno por seguridad de los outer joins. La división evita overflow.
El fallo cierra los handles recién abiertos y devuelve error, no cursor parcial.
SQL local conserva su comportamiento.

Es un límite conservador: puede rechazar un join selectivo cuyo resultado real
sería menor. WHERE, TOP e índices no lo evitan. Para trabajo batch de confianza,
usar ejecución local o dividir el conjunto de datos.

No es un plazo de tiempo, un límite exacto del allocator ni un límite de memos.
Backends externos, DLL nativas, UDF de scripts, sentencias repetidas
y varias etapas UNION/derivadas necesitan presupuestos separados. Siguen
vigentes los límites anteriores de input SQL y recursión.

Los destinos UPDATE/DELETE/MERGE ya usan el mismo preflight de source. Ver
[Prevalidación DML remota](security-sql-dml-preflight.md).
