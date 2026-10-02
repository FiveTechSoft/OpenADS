# Credenciales del servidor

Las credenciales `auth_user` se convierten al registrarse en verificadores
PBKDF2-HMAC-SHA256, con 100.000 iteraciones y sal aleatoria de 128 bits del
sistema operativo. La comparación del hash es de tiempo constante.
Esto no oculta contraseñas pasadas por la línea de comandos o guardadas en
el INI, ni cifra el transporte TCP. La migración de diccionarios es aparte.

Los fallos de login remoto se guardan por IP y usuario entre conexiones.
Hay espera exponencial de 1-60 segundos, sin dormir el worker, y bloqueo
por 5 minutos al alcanzar el máximo. El valor por defecto es 5; el
`ADS_DD_MAX_FAILED_ATTEMPTS` del diccionario se interpreta como decimal
 o entero u16, limitado a 1-100. Cero conserva el valor seguro por defecto.
Las entradas inactivas caducan a los 15 minutos; el registro está acotado.
Un proxy puede agrupar todos los clientes bajo la misma IP. El bloqueo
por usuario también puede usarse para denegar temporalmente su login.
