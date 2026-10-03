# Validacion de cabecera de cifrado ZIP tradicional

La extraccion valida el ultimo byte de comprobacion de la cabecera ZipCrypto
antes de abrir el destino. Las entradas cifradas requieren contrasena no vacia;
se rechazan indicadores de cifrado fuerte no soportado. Se comprueba el byte alto
del CRC o el de la hora DOS cuando hay indicador de descriptor. Las entradas
sin cifrar ignoran una contrasena proporcionada.

Esto cierra el caso de archivos cifrados vacios aceptados sin contrasena o con
una contrasena incorrecta al faltar la comprobacion de cabecera. El formato
tradicional solo ofrece una comprobacion de 8 bits y puede aceptar colisiones,
especialmente en archivos vacios. ZipCrypto no es cifrado autenticado ni adecuado
para proteger copias sensibles frente a un atacante decidido. La auditoria
continua; las carreras de enlaces y la preservacion tras fallos de sobrescritura
siguen siendo puntos independientes.
