# Listener de datos con TLS nativo

Compila con OPENADS_WITH_TLS=ON. Arranca openads_serverd con
`--tls_cert server-chain.pem --tls_key server-key.pem`. Se necesitan ambos.
La clave PEM sin contraseña debe ser legible solo por la cuenta del servicio.
El arranque rechaza archivos ilegibles, PEM inválido y claves que no coincidan
con el certificado. Todos los puertos TCP de datos, incluidos los adicionales,
exigen entonces TLS sin fallback plano. El cliente usa tls://, CA de confianza
y un hostname que coincida con el SAN del certificado.

Los callbacks BIO de mbedTLS usan el socket aceptado. Handshake y lecturas son
no bloqueantes tanto en reactor como en el bucle clásico. Handshake/Connect
vence a los 30 segundos. Las respuestas tienen backpressure de un frame y
plazo de escritura de 30 segundos. Un cliente detenido no espera activamente
dentro de un worker.

Studio no adquiere HTTPS: necesita proxy TLS y firewall. Sin los flags TLS,
el TCP plano legado sigue disponible con aviso al abrir una interfaz externa.
Los usuarios se autentican con Connect, no con certificados de cliente.
Las claves privadas no se registran. La renovación exige reiniciar el servicio.
