# Autorización de APIs nativas de esquema

Conexiones de diccionario identificadas no admin ya no pueden usar CreateTable,
DropTable, RestructureTable, CreateIndex61, PackTable, ZapTable o Reindex sobre
tablas de producción, ni con DML full o rights desactivado. Sin DD y administración
local anónima conservan compatibilidad. Admin mantiene autoridad de esquema.

Crear cursores SQL internos usa permiso interno limitado a esa conexión y esa
llamada CreateTable. Un nombre que parezca temporal no lo obtiene. Mantenimiento
de resultados materializados registrados sigue permitido para el ERP; un cursor
de fuente real no está exento. No cubre todas las APIs de fila/índice/copia.
Esas y la contraseña ZIP siguen como superficies de auditoría separadas.
