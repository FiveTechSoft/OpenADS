# Presupuesto cooperativo compartido para SQL nativo

ExecuteSQL remoto establece un presupuesto por arbol sincronico de llamadas:
1.000.000 comprobaciones y plazo cooperativo de 5 segundos. Las llamadas ABI
anidadas no lo reinician. Se contabilizan navegacion, lectura y escritura de
campos nativos, evaluacion de scripts e indices y comparaciones de ordenacion.
El SQL local conserva su comportamiento. Si se detecta agotamiento se cierra el
cursor resultado y se devuelve fallo; CATCH no restaura el presupuesto.

Complementa los limites de entrada, forma, UNION, subconsultas y scripts. No es
un timeout duro, cuota completa de memoria ni transaccion. No interrumpe llamadas
de disco o drivers SQL bloqueadas. Las comparaciones mantienen su orden y el
sort termina antes de comprobar el resultado. Puede haber trabajo sin instrumentar
entre controles. Siguen pendientes la memoria de resultados externos, cancelacion
por driver y asignaciones acumuladas. Una escritura DML previa puede permanecer
si no se revierte una transaccion aplicable. No se debe reintentar a ciegas un
fallo de plazo. La auditoria sigue abierta, incluidas las carreras de ancestros
y enlaces de archivos.
