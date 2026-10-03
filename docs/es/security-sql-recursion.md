# Profundidad de reentrada SQL

AdsExecuteSQLDirect acepta como máximo 8 llamadas anidadas por hilo.
Procedimientos, triggers, SQL embebido en scripts y reentrada UNION comparten
el contador. Una recursión excesiva devuelve error en vez de agotar la pila.
El contador se libera al salir; el fallo no bloquea sentencias posteriores.
Se aplica local y remotamente. Las cadenas legítimas muy profundas deben
simplificarse. La traza de triggers deja de imprimir SQL con posibles secretos.

No es un límite de tiempo de consulta. DLL nativas, bases externas, scans,
joins y memoria de resultados requieren presupuestos separados.
