# Autorizacion de APIs nativas de fila, indice y copia

Usuarios identificados de diccionario requieren permisos efectivos SELECT,
INSERT, UPDATE o DELETE sobre la ruta real de la tabla. Un alias del cliente,
rights=0 o el modo de apertura no conceden operaciones. Permisos de columna
limitan lecturas y escrituras; fila completa, CRC y copias se deniegan cuando
hay columnas restringidas. Copia de archivos e indices requieren admin.
Las rutas de copia deben permanecer dentro de los directorios de datos.

Resultados SQL materializados registrados conservan trabajo privado; un cursor
sobre la fuente real no es un resultado materializado. Sin DD y anonimo local
conservan compatibilidad. Lecturas internas siguen con preflight SQL. Este fix
acota APIs concretas y no declara finalizada la auditoria de seguridad.
