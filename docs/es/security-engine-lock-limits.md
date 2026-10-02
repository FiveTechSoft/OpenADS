# Límites de locks del motor remoto

Las tablas del motor con propietario remoto conservan como máximo 4096 locks
de registro, incluidos locks virtuales bajo FLock y locks automáticos de
INSERT SQL. Append comprueba el límite antes de escribir la fila vacía; un
rechazo no deja un registro extra. Repetir un lock ya existente sigue permitido;
liberarlo recupera capacidad. Append ADT exclusivo sin lock de registro queda
exento. Las tablas locales conservan el comportamiento anterior.

Complementa el contador de locks explícitos/AppendBlank de la sesión de red.
Es un límite por tabla, no un contador compartido por conexión. Varias tablas
y el trabajo de SQL requieren sus propios presupuestos de recursos/ejecución.
