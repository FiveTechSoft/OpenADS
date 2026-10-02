# Refuerzo de seguridad del servidor

El servidor escucha por defecto en `127.0.0.1`. Para escuchar en red sin
`auth_user` se exige `--allow_anonymous` o `allow_anonymous=true` en el INI.
La misma restricción se aplica a Studio sin `http_user`. Un bind no local
muestra siempre un aviso de tráfico TCP en claro. Esta medida reduce la
exposición, pero no implementa TLS nativo: configure un proxy TLS y un
cortafuegos que impida acceder directamente a su backend TCP.
El formato del protocolo no cambia. Los despliegues LAN antes anónimos
necesitan configurar usuarios o aceptar explícitamente ese riesgo.
