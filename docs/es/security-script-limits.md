# Límites de ejecución de scripts

Cada programa tiene un millón de pasos. Cada sentencia e iteración consume
presupuesto, incluso un WHILE vacío. Al agotarse falla la ejecución; TRY/CATCH
no lo repone. Esto detiene bucles infinitos simples, pero no limita el tiempo
de SQL embebido, DLL o operaciones de disco. Los valores del script se escapan
como literales SQL; las aplicaciones deben enlazar valores no confiables.

Límites del parser: fuente 1 MiB, 4096 tokens, 128 niveles de anidación.
