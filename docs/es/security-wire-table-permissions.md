# Permisos de diccionario en operaciones wire directas

Sesiones wire identificadas con diccionario autorizan tablas antes de los
handlers del motor. Abrir tabla protegida requiere SELECT aunque el cliente
quite rights. Alias y archivo/path comparten objeto ACL. APPEND requiere INSERT,
actualizar campo/registro y bloquear requieren UPDATE; DELETE requiere DELETE.
Cambios de esquema, mantenimiento, archivos raw y archivos comprimidos requieren
admin en estas sesiones.

Cambio intencional: abrir directamente tablas con columnas restringidas se
rechaza porque el formato de fila wire incluye la fila física completa. Usar
SQL con columnas permitidas. Apertura exclusiva requiere admin. Cursores sin
origen de autorización comprobable no se pueden mutar por usuarios no admin.
Sesiones sin DD conservan su política, incluido EnableFileFunc. No equivale a
una auditoría completa de columnas o backends externos.
