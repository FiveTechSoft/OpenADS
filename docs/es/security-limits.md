# Límites de seguridad del protocolo

Antes de Connect: máximo 64 KiB por frame; solo Hello, Connect, MgConnect y Disconnect.
Después de Connect se mantiene el máximo de 16 MiB. Se rechazan NUL, colas
malformadas de capacidades y un segundo Connect. Los frames agrupados se
autorizan uno por uno, no por el hecho de compartir una lectura TCP.
El handshake tiene 30 segundos; la inactividad 5 minutos; un frame parcial
30 segundos absolutos. Máximo 256 tablas/cursors y 64 mutex creados por sesión.
Mutex Lock remoto falla inmediatamente si hay contención: el cliente debe
reintentar. No se permite bloquear los demás clientes del mismo worker.
SetFields valida el número contra los bytes restantes antes de reservar
memoria y limita cada lote a 2048 campos. No se añade protección contra
replay criptográfico: TLS sigue siendo necesario para confidencialidad.

Los locks explícitos y de AppendBlank tienen un máximo de 4096 por sesión. Los locks de INSERT SQL siguen en revisión.

Fetch/FetchWhere rechazan campos mayores de 65535 bytes o respuestas mayores
de 16 MiB en vez de truncar longitudes del protocolo. FetchWhere falla si examina
más de 100000 filas por solicitud; use consultas selectivas o páginas menores.
La telemetría de gestión no almacena el texto SQL porque puede contener secretos.
MgConnect verifica las credenciales de servidor, incluido el password que el
cliente anterior ignoraba. Sin ellas solo se permite lectura en loopback.
Kill/reset y demás mutadores exigen administración autenticada. El cliente
actualizado transmite la contraseña; los anteriores deben actualizarse para
administrar servidores con credenciales.
