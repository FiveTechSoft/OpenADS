# Límites de entrada SQL remota

ExecuteSQL remoto rechaza NUL en vez de ejecutar un prefijo truncado. Máximo
1 MiB de fuente, 4096 unidades léxicas y 128 niveles de paréntesis. La misma
validación se aplica al volver a SQL desde procedimientos/triggers mediante
una conexión ABI de propietario remoto. El contenido de literales, identificadores
entre comillas y comentarios no cuenta como anidación ni operadores.

Son comprobaciones de recursos, no un filtro de inyección SQL. La aplicación
autorizada sigue pudiendo ejecutar SQL y debe enlazar los valores no confiables.
No limitan profundidad recursiva de procedimientos SQL, árboles de expresión
lineales, filas, joins, asignación de resultados, disco lento ni tiempo de ejecución.
Esos caminos requieren presupuestos separados.
