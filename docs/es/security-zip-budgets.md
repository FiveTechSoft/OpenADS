# Limites de recursos para archivos ZIP remotos

La creacion, extraccion y listado remotos aplican por llamada al motor:
100.000 entradas, 1 GiB de datos de origen o expansion declarada, 64 MiB de
nombres y comentarios (el listado y la extraccion contabilizan 64 bytes extra
por entrada) y un plazo cooperativo de 30 segundos. El uso local conserva sus
valores sin limite. La creacion valida los tamanos antes de borrar un ZIP previo
y rechaza un origen que crezca durante la lectura. La extraccion valida el
tamano declarado antes de abrir el destino y limita los bytes reales a ese
tamano. Los listados no acumulan entradas fuera del presupuesto.

El plazo se comprueba entre entradas y bloques, no interrumpe llamadas de disco
bloqueadas. La comprobacion previa de la conexion y la extraccion tienen plazos
separados. Pueden quedar archivos extraidos antes de fallar una entrada posterior.
La preservacion general tras fallos de sobrescritura y las carreras de enlaces
simbolicos siguen siendo puntos de auditoria independientes. No hay una cuota
global de disco ni se declara terminada la auditoria de seguridad.
