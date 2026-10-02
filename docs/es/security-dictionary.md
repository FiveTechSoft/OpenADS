# Autenticación y administración del diccionario

Las contraseñas nuevas se guardan como PBKDF2-SHA256 con sal. Los valores
antiguos en claro se verifican y migran al hacer login válido. El login con
nombre verifica siempre la contraseña, aunque LOG_IN_REQUIRED esté desactivado.
La propiedad 1101 pasa a ser de solo escritura en la API pública.
Las mutaciones DD, los procedimientos administrativos y el registro de una
DLL nativa requieren DB:Admin. El bootstrap anónimo local sigue disponible,
pero nunca autoriza administración remota. Estas medidas cambian comportamientos
inseguros anteriores. El transporte TCP todavía necesita protección TLS externa.
