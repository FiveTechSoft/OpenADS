# Autorización de SQL nativo en diccionarios

SQL nativo con usuario autenticado comprueba permisos de operación aunque
AdsStmtSetTableRights sea cero o el caller pida ignorar derechos. Un flag del
statement no autoriza eludir grants del diccionario. SELECT comprueba todas
las fuentes principales/join y subconsultas inline WHERE/CASE/FILTER antes
de materializar. Miembros UNION/derivados reentran por la misma frontera.
Alias y nombres de archivo se resuelven al objeto protegido. UPDATE, INSERT
y DELETE conservan sus permisos de operación separados.

Sin diccionario o ACL se mantiene abierto. El setup local anónimo y los
privilegios admin conservan comportamiento. El cambio es intencional para
usuarios que dependían de que SQL nativo ignorase ACL explícitas. Backends
externos y permisos por columna en expresiones/fuentes siguen siendo áreas
de auditoría: el fix trata permisos de tabla, no todos los caminos de columnas.
No cambian protocolo ni formatos de archivo.
