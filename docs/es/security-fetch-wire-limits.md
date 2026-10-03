# Limites del protocolo Fetch y errores al leer campos

Fetch/FetchWhere representa cada celda con longitud u16. Main ya rechazaba
valores de mas de 65.535 bytes y aplicaba un limite de respuesta de 16 MiB
(desde 58a357a2). Las pruebas de red cubren memos de 65.535, 65.536 y 70.000
bytes en Fetch/FetchWhere de tablas y Fetch de cursores SQL. El valor maximo
se devuelve intacto; los valores mayores fallan.

La correccion nueva rechaza errores de lectura y columnas desconocidas, en
vez de devolver una celda vacia como exito. Los nombres de columna de cursor
SQL que se truncarian en el buffer de 64 bytes fallan sin consultar otro nombre.
Se conservan las comprobaciones de tamano y el formato de red. Siguen abiertos
el presupuesto general de ejecucion SQL y las carreras del sistema de archivos.
