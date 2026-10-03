# Comprobacion de rutas destino ZIP

Cada destino se resuelve dentro del directorio de extraccion antes de crear o
abrir archivos. Symlinks existentes y directorios enlazados que apuntan fuera se
deniegan tambien en modo plano con overwrite. Nombres absolutos y '..' siguen
rechazados por la comprobacion lexical.

Cierra la fuga reproducida por symlinks ya existentes, no carreras concurrentes.
Un escritor local que sustituya directorios durante la operacion puede competir
con check/open. Proteja el directorio de datos de escritores no confiables;
operaciones seguras por handles siguen en auditoria. Limites ZIP, ZipCrypto y
recuperacion de overwrite fallido no forman parte de este fix.
