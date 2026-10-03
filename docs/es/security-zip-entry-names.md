# Nombres ZIP completos

Enumeracion y extraccion leen el nombre completo del directorio central antes
de elegir destino. Nombres mayores que el buffer anterior de 1024 bytes ya no
se truncan silenciosamente. Nombres con NUL, vacios o fuera del rango del campo
ZIP se rechazan. Se conservan las comprobaciones de rutas y raiz de extraccion.

No cierra limites de recursos, carreras concurrentes, recuperacion de overwrite
fallido ni limitaciones de ZipCrypto. La auditoria sigue abierta.
