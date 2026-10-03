# Preflight de trabajo de subconsultas en predicados remotos

SELECT remoto nativo de una sola fuente comprueba las subconsultas antes de
compilar predicados o exponer cursor. WHERE, condiciones CASE y FILTER de
agregados comparten un millón de visitas estimadas. Cada subconsulta usa el
recuento físico exterior multiplicado por el propio, con overflow comprobado
y suma acumulativa. Su fuente pasa además el límite previo 100k/64MiB. Árboles
anidados comparten la estimación. Los handles de preflight se cierran, también
al rechazar. SQL local no cambia.

La estimación conservadora puede rechazar IN/escalares no correlacionados o
EXISTS que terminarían pronto. Formas inline derivadas/join no soportadas fallan
ahora remotamente en vez de tratarse incorrectamente. No es deadline ni límite
total de memoria: crecimiento concurrente, predicados de joins, expresiones,
backends externos y DLLs requieren controles propios. Divida batches grandes
de confianza o ejecute localmente. Protocolo y archivos no cambian.
