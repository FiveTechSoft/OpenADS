# Autorización del esquema SQL nativo con diccionario

SQL DROP TABLE/INDEX, ALTER TABLE y CREATE TABLE/INDEX/DATABASE requieren rol
administrador del diccionario cuando hay uno conectado. SELECT o incluso DML
completo de tabla no autorizan borrar el archivo, cambiar su estructura o
reconstruir índices de producción. Se comprueba antes de leer fuentes y cambiar
archivos; flags de derechos del cliente no lo evitan. SQL devuelve envoltorio
7200 con error nativo de acceso 7079.

Cambio intencional para usuarios identificados no admin. Administración local
anónima y operación sin diccionario conservan compatibilidad. DROP de #temp
sigue privado de la conexión. CREATE TABLE AS SELECT también es admin. La
materialización interna de cursores no concede autoridad SQL de esquema y no
cambia. APIs DDL no SQL y operaciones directas de tabla/archivo wire se auditan
por separado.
