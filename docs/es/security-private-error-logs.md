# Diagnósticos SQL privados

El solicitante sigue recibiendo los diagnósticos ACE completos. ads_err.dbf y
ads_err.log conservan código de error SQL y contexto, pero no el SQL ni texto
diagnóstico: errores de parseo, SQL embebido y RAISE pueden citar contraseñas
o datos privados. Las trazas ocultan connection strings de AdsConnect101,
texto del último error y errores SQL remotos.

Es una reducción intencional del detalle almacenado. Reproducir el fallo en
un entorno controlado y consultar AdsGetLastError directamente. Los logs ya
existentes no se reescriben: rotarlos o eliminarlos conforme a la política de
retención si versiones anteriores guardaron sentencias sensibles. Otros logs,
paths del operador y logging de backends externos necesitan revisión y control
de acceso propios.
