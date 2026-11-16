# Configuración de los límites de trabajo SQL remoto

El servidor aplica estos límites a cada sentencia SQL que ejecuta un cliente
remoto. El SQL local (acceso directo a archivos) no tiene ninguno, y un
Advantage Database Server real tampoco.

| Clave (`openads.ini`)    | Variable de entorno               | Por defecto | Se aplica a |
|--------------------------|-----------------------------------|-------------|-------------|
| `remote_sql_max_rows`    | `OPENADS_REMOTE_SQL_MAX_ROWS`     | 100000      | registros de cada tabla origen / destino de UPDATE / DELETE ([preflight](security-sql-work-shape.md)) |
| `remote_sql_max_mb`      | `OPENADS_REMOTE_SQL_MAX_MB`       | 64          | volumen estimado de filas de ancho fijo, MiB |
| `remote_sql_max_steps`   | `OPENADS_REMOTE_SQL_MAX_STEPS`    | 1000000     | pasos de trabajo del motor por sentencia |
| `remote_sql_timeout_ms`  | `OPENADS_REMOTE_SQL_TIMEOUT_MS`   | 5000        | tiempo real por sentencia |

`0` significa sin límite. Un valor ausente o no numérico mantiene el valor por
defecto. La variable de entorno tiene prioridad sobre la clave del ini.

Las claves van en el `openads.ini` del servidor (el indicado con `--config`, o
el que está junto a `openads_serverd.exe` / `openace32.dll`). Reinicie el
servidor después de cambiarlas. Por ejemplo, para que una aplicación de
confianza en la LAN ejecute SQL de mantenimiento (`UPDATE` / `DELETE` de
millones de filas, `SELECT ... INTO`):

```ini
[server]
remote_sql_max_rows   = 0
remote_sql_max_mb     = 0
remote_sql_max_steps  = 0
remote_sql_timeout_ms = 0
```

Subir los límites cambia la protección frente a sentencias costosas de
clientes remotos por la posibilidad de ejecutar sentencias grandes. Mantenga
los valores por defecto en servidores accesibles por clientes no confiables.

Cuando un límite detiene una sentencia, el error del cliente lo nombra, por
ejemplo `ExecuteSQL: server-side exec failed (remote SQL source budget
exceeded)`. Los demás errores SQL del servidor siguen siendo genéricos.

Una sentencia detenida por un límite nunca deja un resultado parcial: un
`SELECT ... INTO <tabla>` / `CREATE TABLE ... AS SELECT` que falla elimina la
tabla que creó, y una subconsulta que no pudo leerse completa hace fallar la
sentencia en lugar de actuar sobre una lista parcial.
