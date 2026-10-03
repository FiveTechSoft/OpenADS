# Verificacion de longitud al extraer ZIP

La extraccion compara bytes escritos con el tamano declarado de cada entrada,
ademas del CRC de minizip. Un EOF anticipado por password incorrecto o datos
corruptos puede dejar bytes pendientes y evitar la verificacion CRC interna.
Ahora se devuelve error y se elimina la salida parcial; no se acepta exito
con bytes declarados que nunca se escribieron.

Cierra el fallo reproducido de EOF anticipado, no todos los riesgos ZIP.
ZipCrypto tradicional conserva compatibilidad y no es cifrado autenticado
moderno. Entradas vacias cifradas y limites de recursos siguen en auditoria.
Este cambio no restaura automaticamente un archivo sobrescrito si falla.
