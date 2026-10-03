# Límites de lectura de memos remotos

Las tablas nativas remotas limitan cada valor memo/binario leído a 8 MiB.
FPT comprueba la longitud declarada antes de reservar memoria. ADM comprueba
la longitud del registro antes de reservar. DBT comprueba cada bloque antes
de ampliar el string. El control está en el store, no después de crear el valor.
La lectura local conserva su margen. Todas las lecturas FPT/ADM comprueban
además la longitud frente al tamaño actual del archivo antes de reservar:
un archivo truncado o manipulado no fuerza antes una reserva de varios GiB.

Cubre lecturas de tablas nativas y SQL que accede a esos campos. No es un
presupuesto total del resultado SQL: muchos memos permitidos, resultados de
expresiones, etapas repetidas/derivadas y backends externos necesitan controles
propios. Las lecturas remotas grandes devuelven error; los predicados que ya
tratan errores de campos como false mantienen ese comportamiento. No cambian
los formatos de archivo ni protocolo. Use herramientas locales para valores
de confianza mayores que el límite remoto.
