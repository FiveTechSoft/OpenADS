# Conservar destinos cuando falla una escritura ZIP

La creacion escribe en un directorio temporal hermano creado en exclusiva y
publica el ZIP solo tras cerrar correctamente. La extraccion hace lo mismo por
archivo y publica tras validar lectura, longitud y CRC. Los fallos borran el
temporal, no el destino anterior. En POSIX se usa enlace duro sin sobrescritura
y rename con sobrescritura. Windows usa MoveFileEx con la opcion correspondiente.
Antes de publicar la extraccion se comprueba otra vez la ruta dentro del destino.

Se conserva el archivo anterior ante fallos normales de lectura, validacion o
publicacion. Se puede incluir el ZIP previo entre los origenes sin borrarlo antes
de leerlo. Si el sistema no admite enlaces duros, la publicacion sin sobrescritura
falla sin reemplazar el destino. El archivo nuevo no conserva los permisos, ACL,
propietario o identidad de enlaces duros del anterior; en Windows el temporal
hereda la ACL del directorio padre.

Es reemplazo por archivo, no transaccion global ni garantia ante cortes. Un corte
puede dejar directorios temporales. Las carreras de sustitucion de directorios o
enlaces requieren un diseno de operaciones relativas a handles. La auditoria
sigue abierta.
