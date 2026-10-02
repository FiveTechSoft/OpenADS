# Presupuesto de Aggregate remoto

Aggregate remoto acepta como máximo 32 funciones y 4096 bytes de expresión.
Se rechazan funciones desconocidas, NUL y bytes sobrantes. Tablas mayores de
100000 filas físicas se rechazan antes de mover el cursor. Las demás tienen
un presupuesto cooperativo de dos segundos, comprobado entre filas; no se
interrumpe una operación de disco o expresión dentro de una fila. Al agotarse
se devuelve error, nunca un total parcial. Valores mayores de 65535 bytes
no pueden truncar las longitudes del protocolo.

Son límites intencionados del opcode remoto. Para informes mayores use
agregados SQL. El agregado local y la ejecución SQL usan caminos distintos
y no quedan limitados por este cambio.
