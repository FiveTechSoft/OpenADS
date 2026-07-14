---
title: Novedades
layout: default
parent: Inicio (ES)
nav_order: 0
permalink: /es/novedades/
---

# Novedades (v1.0.0-rc29 → v1.8.12)

Esta página resume los cambios más destacados desde la versión
v1.0.0-rc29. Para el historial completo de commits, consulta el
[CHANGELOG](https://github.com/FiveTechSoft/OpenADS/blob/main/CHANGELOG.md).

---

## Destacados v1.8.12

### ENGINE — `Skip` multi-registro sobre filas borradas

Continuación de v1.8.10/v1.8.11: con el filtro de borrados activo, un
browse remoto en orden natural podía mostrar un **duplicado de la fila
anterior** donde antes aparecía un registro borrado, o terminar el
recorrido unas filas antes. `Table::skip` calculaba los saltos
multi-registro con aritmética física de recno; ahora cuenta filas
**visibles**, igual que la ruta con índice y la semántica `SKIP n` de
Clipper. También corrige `dbSkip(n > 1)` LOCAL sobre filas
borradas/filtradas.

Requiere `openads_serverd` actualizado (y `openace64.dll` para modo
LOCAL). Pruebas de regresión: tres casos M12.33 en
`abi_remote_index_nav_test.cpp`.

---

## Destacados v1.8.11

### REMOTO — `SET DELETED ON` antes de conectar

Continuación de v1.8.10: el indicador ahora también llega al servidor
cuando `SET DELETED ON` se ejecuta **antes** de `AdsConnect60` — el
orden que usa cualquier aplicación rddads / FiveWin. El cliente
sincroniza el estado justo después de conectar, y el servidor lo
reaplica a la conexión ABI (creación diferida) usada para la
navegación ordenada/con ámbito.

Requiere `openace64.dll` **y** `openads_serverd` actualizados. Pruebas:
`remote scoped walk honours SET DELETED ON issued before connect`
(+ `… before first ordered op`); sonda de red
`tools/remote_deleted_probe.cpp`.

---

## Destacados v1.8.10

### REMOTO — `SET DELETED ON` en recorridos con ámbito

`AdsShowDeleted(0)` (SET DELETED ON) ahora se propaga al servidor por
wire (`ShowDeleted` 0xDA/0xDB). La navegación indexada con ámbito
(`OrdScope` / `AdsSetScope` + `GotoTop`/`Skip`) ya no devuelve filas
marcadas como eliminadas en alias remotos.

Requiere `openace64.dll` **y** `openads_serverd` actualizados. Prueba:
`remote AdsSetScope with SET DELETED ON skips deleted rows`.

---

## Destacados v1.8.1

### ABI — relleno de claves `OrdScope` en campos carácter

Harbour envía el ámbito con longitud recortada, pero las claves CDX van
rellenadas con espacios. `setScopeTop` / `setScopeBottom` en un campo de
orden de trabajo podía dejar `GotoTop` en EOF en **local y remoto**.
`AdsSetScope` ahora rellena hasta `key_length`. Prueba: `QA-D`.

---

## Destacados v1.8.0

### CDX — colación OEM NTXPL852 / PL852

`AdsSetCollation` acepta `NTXPL852` y `PL852` para ordenar índices CDX
según CP-852 polaco. Ł (0x9D) queda entre L y M en build, seek, insert
y reindex.

### CDX — `REINDEX` en bloque

`Table::reindex()` en tags CDX usa `build_bulk()` (misma ruta rápida que
`CREATE INDEX`).

### Pruebas

19 pruebas unitarias nuevas (motor, driver CDX y ABI).

---

## Destacados v1.7.0

### REMOTO — `AdsSetScope` / `OrdScope`

Los límites superior/inferior de `OrdScope` se enviaban al servidor, pero
`GotoTop`/`Skip` ignoraban el ámbito. Corregido en `session.cpp` y
`remote_index_nav.cpp`. Requiere `openace64.dll` y `openads_serverd`
actualizados.

### Docs — aviso de rollback CDX

[Migrando desde ADS](migrando-desde-ads/) documenta que los CDX escritos por
OpenADS usan el header `RCHB` de Harbour y no son legibles por SAP ACE
(error 7017).

---

## Destacados v1.6.5

### REMOTO — `OrdKeyCount()` y `AdsGetDate()`

- **`OrdKeyCount()` devolvía 0** en alias remotos — rejillas xBrowse vacías.
  Nuevo opcode wire `GetKeyCount`.
- **`AdsGetDate()` fallaba** en campos `ADS_DATE` remotos cuando rddads
  pasaba un handle `RemoteIndex`.

---

## Destacados v1.6.4

### REMOTO — Correcciones para FiveWin TDataBase / ADSRDD (fechas y escrituras sin lock)

- **Campos Date**: `FieldGet` / `AdsGetJulian` sobre columnas `ADS_DATE` ya
  no crashea con ACCESS_VIOLATION en modo remoto. Resolución segura de
  ordinales + caché de fila + normalización a `YYYYMMDD` en el servidor.
- **FieldPut sin lock**: Ya no da AV. Devuelve `AE_RECORD_NOT_LOCKED` (5035)
  cuando se escribe sin `RLock` previo. El idiom "escribir el mismo valor
  para probar el lock" ahora funciona y genera `EG_UNLOCKED` esperado.
- Todos los paths de Get/Set remoto ahora son seguros con ordinales como
  punteros pequeños; lecturas repetidas y post-open/EOF son estables.
- `AdsGetAllLocks` ya no crashea (5000) en handles remotos.
- Mejor forwarding de locks y manejo de writes tras `AppendRecord`.

Requiere `openace64.dll` y `openads_serverd` actualizados. Ver CHANGELOG
completo.

## Destacados v1.6.3

### REMOTO / FWH — xBrowse sobre `tcp://`

Correcciones para Harbour `ADSCDX` + FWH `xBrowse` / `TDataBase` con tags
CDX de producción (`OrdSetFocus`, p. ej. `CUSTNAME`):

- **Scrollbar (`AdsKeyNo`)** — posición lógica en el orden (`1..n`), no
  `RecNo` físico; `AdsGetRelKeyPos` / `AdsSetRelKeyPos` en índice remoto.
- **`DbGoto` tras pintar** — el servidor sincroniza el cursor ABI del
  índice en `GotoRecord`; las filas dejan de cambiar en cada `Refresh`.
- **Subir en el tope** — `AdsAtBOF` tras `Skip(-1)` en la clave #1; evita
  repetir la primera fila.

Requiere `openace64.dll` actualizado y `openads_serverd` con el sync en
`GotoRecord`. Tests en `abi_remote_index_nav_test`.

---

## Destacados v1.6.2

### Nombres de tags CDX SAP y `FieldGet` en fila nueva

Nombres correctos en CDX compuesto (`CUSTNAME`); `AdsGetField` en blanco
en BOF/EOF para `td_blankrow()` de FWH.

---

## Destacados v1.5.1

### Seguridad y endurecimiento remoto

Path jail en `Connect` remoto, unlock anidado coherente en `LockMgr`,
verificación TLS por defecto, y escrituras remotas de memo/Unicode/fecha/raw.

### Harbour smoke CI

Job Windows en GitHub Actions + scripts `run_harbour_smoke.ps1` /
`bootstrap_harbour_ci.ps1`.

### ABI remoto (Fase 2)

`AdsSetRelation`, `AdsSetRecord` / `AdsGetRecord`, `AdsCustomizeAOF` en
`tcp://`. `AdsAggregate` / `AdsFetchWhere` local en tablas DBF.

### Plus — escritura navegacional SQLite y MSSQL

Backends SQLite y MS SQL Server nativo (TDS) con
`AdsAppendRecord` / `AdsSetString` / `AdsWriteRecord` /
`AdsDeleteRecord` — paridad con MariaDB, PostgreSQL, Firebird y ODBC.
Relleno de `ADS_STRING` en lectura MSSQL.

### Motor

Fixtures ADT/ADI en el repositorio; cabecera VFP `0x32` (autoinc + nullable).

---

## Nuevas Funcionalidades

### Driver de Tablas con SQLite

Un driver alternativo de tablas respaldado por SQLite está
disponible detrás del flag de CMake `OPENADS_WITH_SQLITE`. Cuando
se habilita, el motor puede abrir y manipular tablas a través de
un backend SQLite, proporcionando una capa de almacenamiento
alternativa. Nuevos archivos fuente:

- `src/sql_backend/sqlite_backend.cpp`
- `src/sql_backend/sqlite_connection.cpp`
- `src/sql_backend/sqlite_table.h`
- `src/sql_backend/sqlite_index.h`

### Backends SQL — PostgreSQL / MariaDB / ODBC (OpenADS Plus)

OpenADS ahora puede abrir tablas en **PostgreSQL**, **MariaDB /
MySQL** y cualquier motor accesible por **ODBC** detrás de la ABI
ACE, elegido por la URI de conexión (`postgresql://` /
`mariadb://` / `odbc://`), igual que el backend SQLite. Desde la
aplicación la tabla se comporta como cualquier área de trabajo —
navegación, lectura de campos y SEEK por columna funcionan.

Estos cuatro backends SQL viven detrás de un único **registro
enchufable de backend-ops**: cada uno registra una struct
`BackendTableOps` (17 punteros a función que reflejan las ops de
tabla), de modo que las ~17 funciones ABI de navegación / campos
quedan agnósticas al backend en vez de multiplicar un bloque `if`
por backend. Añadir otro backend (p. ej. Firebird, o MSSQL /
Oracle vía ODBC) es una struct de ops más una línea de registro.
Las rutas DBF / ADT local nativas y la remota `tcp://` quedan
intactas. Los identificadores se validan a ASCII seguro y los
valores de SEEK usan parámetros preparados (sin concatenar
strings). Ver `docs/OPENADS_PLUS.md`.

### Parches de Validación ADT (F1–F7, R1–R3)

La validación de tablas ADT ahora incluye un conjunto completo
de parches estructurales (F1–F7) y verificaciones a nivel de
registro (R1–R3), reforzando las garantías de integridad para
archivos `.adt` producidos por SAP Advantage.

### Creación, Lectura, Escritura y Búsqueda en Índice ADT/ADI Nativo

OpenADS ahora puede operar de extremo a extremo con archivos
nativos `.adt` / `.adi` / `.adm`:

- **Crear** — `AdsCreateTable(ADS_ADT)` escribe una cabecera de
  tabla válida, descriptores de campo y un almacén de memo
  `.adm` opcional.
- **Escribir** — `AdsAppendRecord` / `AdsWriteRecord` persisten
  filas y payloads de memo.
- **Leer** — Reabrir, obtener campos, conteo de registros,
  ida y vuelta de memo.
- **Índice** — `AdsCreateIndex61` construye bolsas `.adi`
  (primer tag vía `AdiIndex::create`, tags adicionales vía
  `add_tag`).
- **Buscar** — `AdsSeek` en claves ADI de carácter y numéricas.
- **AUTOINC** — contador sembrado desde filas existentes al
  abrir; bytes 139–143 del descriptor permanecen en cero en
  disco.
- **Layout de memo ADM** — bloques de 8 bytes con un prefijo
  de metadatos de 1024 bytes.

### Ruta de Escritura ADI

El driver de índices ADI ahora soporta operaciones de escritura
— `insert`, `erase` y `flush` — incluyendo la decodificación de
recno de hoja densa y búsqueda de claves de carácter. Esto
completa el ciclo de lectura-escritura para índices ADI.

### Despacho de Triggers (BEFORE / INSTEAD_OF / AFTER)

Los triggers ahora se ejecutan con el despacho de temporización
adecuado:

- **BEFORE** — se ejecuta antes de la sentencia DML.
- **INSTEAD_OF** — reemplaza la DML en vistas.
- **AFTER** — se ejecuta tras la ejecución exitosa.

Se soportan orden por prioridad, tabla `__error` para fallos,
procedimientos almacenados `sp_DisableTriggers` /
`sp_EnableTriggers`, y claves compuestas de triggers en
`system.triggers`.

### Interfaz de Gestión DA-Web

El reemplazo del Data Architect basado en navegador (**DA-Web**)
ha recibido un trabajo extenso:

- **Edición en línea de celdas** con seguimiento de cambios
  visuales.
- **Gestión de índices** — guardar y eliminar índices vía API.
- **CRUD de Triggers** — agregar, eliminar y editar triggers con
  validación en línea.
- **Barra de filtros AOF (Rushmore)** en el explorador de tablas.
- **Resaltado de sintaxis SQL ADS** con colores similares a
  HeidiSQL.
- **Visor de código de procedimientos almacenados / funciones**
  con parámetros y Save-to-DD.
- **Explorador de tags de índice**, etiquetas de tipos de campo
  y scripts SQL.
- **Menú de conexión** — Nuevo DD, Abrir DD, Tablas libres.
- **Pestañas de Permisos Efectivos** y **Miembros** en paneles de
  usuario/grupo.
- **Desplegables de tags RI** poblados desde archivos `.add`
  binarios.

### openmonitor — TUI y Panel Web

Una nueva herramienta `openmonitor` proporciona tanto una interfaz
de terminal (TUI) como un panel web para monitorear y administrar
`openads_serverd`.

### OpenADS Studio — Responsive (teléfono / tablet)

La consola web Studio ahora se adapta a pantallas pequeñas — se
puede usar desde un teléfono o tablet, no solo desde un navegador
de escritorio. Por debajo de ~768 px la lista de tablas se
convierte en un **drawer** deslizante (☰ en el header, fondo
atenuado, cierre automático al elegir); la barra de pestañas hace
scroll horizontal y los formularios / modales se reorganizan en una
columna con objetivos táctiles más grandes. Se corrige además un
bug antiguo del tema oscuro — las variables CSS `--panel` /
`--panel-2` / `--border` eran auto-referenciales, por lo que
paneles y bordes se mostraban transparentes.

### Rendimiento de escaneo remoto (prefetch secuencial)

Un escaneo hacia adelante sobre el wire `tcp://` costaba más o menos
un round-trip TCP por registro (`Skip` + `AtEOF` + `IsFound`). Un
camino de prefetch secuencial (negociado con un flag de capacidad en
Connect) ahora adjunta un bloque de lookahead a los acks de
`Skip` hacia adelante; el cliente los sirve localmente y vuelve a
plegar el conteo consumido en el siguiente paso del wire, de modo
que el cursor del servidor nunca se desincroniza. `AdsAtEOF` /
`AdsAtBOF` se responden desde la fila actual cacheada y `AdsIsFound`
desde un flag `Found()` cacheado. Resultado: un escaneo de 50k
registros en loopback es **~3.9× más rápido** (solo NAV) / **~3.3×**
(lectura de 3 campos), con los round-trips de `IsFound` a cero. El
cambio es aditivo y retrocompatible — los clientes antiguos (sin
capacidad anunciada) mantienen el comportamiento del wire idéntico.

### Extensión Nativa PHP (`php_openads`)

Una extensión nativa Zend PHP (`php_openads.dll`) está ahora
disponible para PHP 8.x, proporcionando CRUD completo del DD (35
nuevos métodos `AdsDictionary`), decodificación de campos
date/timestamp y caché de nombres de campo por sentencia.

### Mejoras en la Importación de Diccionario de Datos SAP

- `import_dd` ahora copia archivos de memo `.am` y decodifica
  cuerpos de funciones encriptados.
- Importación de membresía de grupos (DB:Admin, DB:Backup,
  DB:Debug) desde archivos `.add` binarios.
- Temporización de triggers capturada desde `system.triggers`.
- `grant_permission` y código de error `AE_SAP_PERMS_NEED_IMPORT`
  para migración de permisos.

### Recuperación de Fallos WAL

La recuperación de fallos WAL (Write-Ahead Log) ahora maneja
registros `APPEND`, completando el modelo de recuperación
ARIES-lite.

### Expansión del SQL del DD

- `CREATE DATABASE`, `GRANT` / `REVOKE`.
- Procedimientos almacenados `sp_*`.
- Tablas virtuales `system.*` (`system.iota`, `system.columns`).
- `AdsDDGet/SetFieldProperty`, triggers, procedimientos
  almacenados, vistas y propiedades de índices.
- Control de acceso por tabla con niveles de permiso de
  usuario/grupo.

### Agregación Server-Side (Tier-3)

`AdsAggregate` ahora soporta `COUNT`, `SUM`, `AVG`, `MIN` y
`MAX` con push-down a backends SQL (SQLite, PostgreSQL, MariaDB,
ODBC). La spec de agregación se valida antes de ejecutar, y los
resultados se sirven a través de un result set basado en handles
(`AdsAggregateCount` / `AdsAggregateValue` /
`AdsAggregateClose`).

### FetchWhere V2

`AdsFetchWhere` ahora sirve escaneos hacia adelante desde un
result set cacheado — sin round-trip por coincidencia. El cliente
recibe filas en lote y las recorre localmente, con recno por fila
opcional (flag `WANT_RECNO`). Los escaneos masivos de `SET FILTER`
sin AOF se enrutan a través de `AdsFetchWhere` con ganancias
significativas de rendimiento.

### Driver ODBC (slice 1–3)

Un driver ODBC completo (`openads_odbc.dll`) está ahora disponible:

- **Round-trip SELECT** con cursores scrollables
  (`SQLFetchScroll`).
- **Acceso tipado a columnas** — `SQLDescribeCol` /
  `SQLColAttribute` / `SQLGetData` despachan a través del vtable
  de ops del backend.
- **Binding posicional de parámetros** vía `SQLBindParameter`.
- **Funciones de catálogo** — `SQLPrimaryKeys` /
  `system.primarykeys`.
- **Emulación de app-lock** — `rLock()`/`fLock()` vía SQL Server
  `sp_getapplock`, locks advisories de PostgreSQL, locks con nombre
  de MariaDB y tabla `OPENADS$LOCKS` de Firebird.

### Ruta de Escritura Nativa (PostgreSQL / MariaDB / Firebird)

`AdsAppendRecord` / `AdsSetField` / `AdsWriteRecord` /
`AdsDeleteRecord` ahora funcionan de extremo a extremo en
backends PostgreSQL, MariaDB y Firebird — sin passthrough ODBC.

### Expansión del Push-Down SQL

Las expresiones de `SET FILTER` y AOF ahora se empujan a SQLite
y PostgreSQL como cláusulas `WHERE` cuando el árbol de expresiones
está dentro del subconjunto optimisable (`try_emit_sql_where`). La
cobertura incluye `$` (contiene), `LEFT()`, `RIGHT()`,
`SUBSTR()` y `UPPER()`.

### Documentación Completa de la API (Portugués)

Las **364 funciones ACE** están ahora documentadas en portugués
(pt-BR) bajo `docs/pt/funcoes/`, cubriendo sintaxis, parámetros,
valores de retorno y ejemplos.

### Corrección de Calling Convention x86 (32-bit)

`ENTRYPOINT` ahora es `__stdcall` (WINAPI) en Win32, coincidiendo
con la convención de llamada de `rddads` de Harbour. Esto corrige
la corrupción de stack cuando apps Harbour de 32-bit llaman a
funciones ACE a través del DLL. El archivo `.def` de x86 y la
librería de importación se actualizan para coincidir. (Reportado
por Jonsson / RusSoft Ltda.)

### CI — Build Leg msvc-x86

Una nueva entrada de matrix `msvc-x86` en
`.github/workflows/ci.yml` asegura que builds de 32-bit se
prueben en cada PR, atrapando breakages exclusivos de x86
(reducción dependiente de bitness, conflictos de firma `SQLLEN*`,
advertencias `/WX`) antes de que lleguen a main.

---

## Correcciones de Errores

### Motor

- **Orden de tags CDX** — `list_tags()` ahora ordena por offset
  del tag-header (orden de creación) en lugar del orden alfabético
  de la hoja. Corrige `DBSETORDER(n)` seleccionando el tag
  incorrecto en bolsas CDX escritas por SAP ADS. (Reportado por
  Jonsson / RusSoft Ltda.)
- **Tamaño de clave de índice de expresión CDX** — las claves de
  expresiones compuestas (p. ej. `UPPER(cName)`) ahora se dimensionan
  desde el ancho fijo natural de la expresión, no desde la longitud
  del contenido del primer registro. El antiguo rtrim truncaba
  claves, causando filas fuera de orden tras reindex en tablas
  grandes. (Reportado por Jonsson / RusSoft Ltda.)
- **Caminado de hojas vacías CDX** — los caminados hacia adelante
  y hacia atrás del índice ahora saltan hojas vacías dejadas por
  `erase()`. Corrige `ADSCDX/5000` en REINDEX / borrado masivo.
  (PR #63)
- **Bits de recno en hoja CDX** — `compute_layout` dimensiona el
  campo de número de registro desde `max_rec`, no solo la longitud
  de clave, para que las tags con claves anchas ya no truncuen
  recnos ≥ 4096. (PR #62)
- **Búsqueda parcial CDX** — `seek_key` compara solo la longitud
  de la clave de búsqueda, para que búsquedas parciales como
  `SEEK "ART-00024800"` coincidan con claves almacenadas
  `"ART-00024800 desc ..."`. (PR #62)
- **FOR condicional en campos lógicos** — el evaluador de
  expresiones de índice ahora trata los campos lógicos como
  numéricos (0/1) en lugar de cadenas truthy, para que `FOR ACTIVE`
  filtre correctamente registros `.F.`. (PR #121)
- **Corrupción por INDEX ON** — previene que `INDEX ON` corrompa
  índices de la tabla fuente. (PR #118)
- **SKIP hacia atrás MSSQL** — error por uno: `abs_n == pos` ahora
  llega a la fila 0 en lugar de reportar BOF. (PR #65)
- **Getters tipados ABI para backends SQL** — `AdsGetDouble`/`Long`/
  `LongLong`/`String` despachan a través del vtable de ops del
  backend, para que PostgreSQL devuelva valores reales. (PR #66)
- **`AdsGetIndexHandle` para backends SQL** — resuelve por nombre
  para tablas PG para que la búsqueda por índice funcione
  extremo a extremo. (PR #66)
- **Formato de clave numérica NTX** — los campos numéricos
  indexados en un bolsillo NTX ahora almacenan claves en el formato
  nativo DBFNTX (magnitud rellenada con ceros + negativos
  complementados) en lugar de texto `STR()` rellenado con espacios.
  La búsqueda nativa `dbSeek(<number>)` de un lector xBase ahora
  coincide con la clave en disco. Los bolsillos de índice
  reabiertos conservan la codificación numérica. (PR #67)
- **`dbSeek` numérico** — rddads envía tipo de clave
  `ADS_STRING` para búsquedas numéricas; el motor ahora lo
  maneja correctamente.
- **`ALIAS->FIELD` en búsqueda numérica** — elimina el prefijo
  `ALIAS->` para que los tags CDX con alias encuentren claves.
- **Reversión de transacciones** — elimina físicamente los
  registros agregados al revertir en lugar de dejar filas
  fantasma.
- **Fuga de estado LockMgr** — `held_` ahora se limpia al
  desbloquear para que el siguiente bloqueo tome un bloqueo real
  del SO.
- **`AdsGetRecordCount`** — ahora respeta `bFilterOption`.
- **`AdsSetRelation`** — falla honestamente cuando es
  apropiado.
- **`seek_key`** — `walk_to_last` ahora honra `SET DELETED ON`.
- **Navegación de tabla vacía** — maneja correctamente tablas
  con cero registros.
- **Navegación de registros eliminados** — estado correcto tras
  saltar filas eliminadas.
- **Recuento de bloqueos LockMgr** — los bloqueos repetidos sobre
  la misma clave ahora se cuentan por referencia; el bloqueo del
  SO se libera solo cuando el último poseedor desbloquea.
- **Comprobación de límites de registros WAL** —
  `TxLog::read_all` valida la longitud de cada campo
  UPDATE/APPEND antes de leer, evitando lecturas excesivas en
  archivos WAL truncados o corruptos.

### ABI

- **Lectura fuera de límites** en formato de clave numérica —
  prevenida.
- **Bits de opción intercambiados** — `ADS_DESCENDING` (0x02) y
  `ADS_COMPOUND` (0x08) se decodificaban incorrectamente en
  `AdsCreateIndex61`.
- **Resolución de nombres de campo insensible a mayúsculas** —
  `field_index` se cachea para mejor rendimiento.
- **`AE_NO_CURRENT_RECORD`** — devuelve 5068 en lugar de 5026.
- **`OrdListAdd`** — vuelve al nombre base cuando una ruta
  relativa prefija doble el directorio de la tabla.
- **Vínculo de helpers trig** — vínculo C++ para helpers `trig_*`
  para silenciar MSVC C4190.
- **Crash de `AdsGetField` en backends SQL** — leer por ordinal
  de campo ya no falla.
- **AdsGetRecordCount en ORDER condicional** — cuenta coincidencias
  FOR correctamente. (PR #100)
- **`AdsSetAOF` para filtros no optimizables** — ahora devuelve
  `AE_INVALID_EXPRESSION` en lugar de éxito, para que rddads
  stock caiga en filtrado del lado del cliente. El comportamiento
  anterior deshabilitaba silenciosamente `SET FILTER` por completo.
  (Reportado por Jonsson / RusSoft Ltda.)

### Driver ODBC

- **Build x86** — `C4100` (parámetro no usado) y `C2733`
  (conflicto de firma `SQLLEN*` vs `SQLPOINTER`) corregidos para
  32-bit MSVC `/WX`. (PR #119)
- **Conformidad DM/ADO** — manejadores de descriptor, `SQLBindCol`
  y mapeo de tipo `BIT` corregidos.
- **`SQL_DRIVER_ODBC_VER` / `SQL_ODBC_VER`** — ahora se reportan
  en `SQLGetInfo`.

### Seguridad DA-Web

- Sanitización de expresiones de filtro AOF.
- Corrección de contención de ruta de raíz de unidad.
- Barrido de seguridad de API en todos los endpoints PHP.
- Rutas de índice RI meta contenidas bajo el directorio DD.
- Límites de tamaño de frame wire para prevenir abuso.

### Plataforma / Build

- **macOS** — advertencias de sign-conversion y unused-function
  corregidas.
- **GCC** — advertencias `-Werror` resueltas (shadow, implicit
  conversion, format-truncation, stringop-truncation).
- **MSVC** — decoración `__stdcall` de x86 y crash de `_wfsopen`
  en x64 corregidos.
- **Clang** — guardia `-Wc2y-extensions` para Apple Clang
  anterior.
- **Colisión de fd 0 en POSIX** — los manejadores de archivo
  ahora se almacenan como `(fd + 1)` para que un fd 0 real
  (devuelto por `open()` cuando stdin está cerrado) no se
  confunda con el centinela "no abierto".
- **Reintento `EINTR` en POSIX** — `pread` / `pwrite` reintentan
  ante interrupción por señal en lugar de fallar la E/S.
- **mmap de longitud cero en POSIX** — `map_readonly` rechaza
  mapeos de longitud cero en lugar de llamar a `mmap` con
  longitud 0.

### CDX

- Bloqueo de cabecera compartido al abrir para que las
  inserciones concurrentes no fallen.
- CDX estructural nombrado por tabla, no por sesión de
  directorio.
- **Refresco del conteo de registros obsoleto en la ruta de
  lectura** — el driver cachea el conteo de registros del DBF al
  abrir; en un despliegue multiusuario un append de otro proceso
  podía dejar la caché desfasada, de modo que un recorrido por
  índice que alcanzaba una fila recién agregada (p. ej. en mitad de
  `REPLACE … FOR`) fallaba con un error 5000 espurio.
  `read_record_raw` / `write_record_raw` ahora releen el conteo en
  disco bajo un bloqueo de cabecera compartido antes de declarar un
  recno fuera de rango (solo ruta lenta — un escaneo normal hacia
  adelante no paga nada).

### Remoto (Wire)

- Crash de use-after-free en consultas de tablas virtuales vía
  TCP eliminado.

---

## Documentación

- **CONTRIBUTING.md** — nueva guía de contribución con flujo de
  PR, política de protocolo y reglas de clean-room.
- **Wire Protocol DD API** — referencia completa de la API del
  Diccionario de Datos (§9) agregada.
- **DA-Web GUIDE.md** — guía completa de usuario para la
  interfaz de navegador DA-Web.
- **README** — actualización completa del estado post-rc29
  cubriendo ruta de escritura ADI, creación ADT, modo AES y
  alcance de DA-Web.
- **Cookbook** — nueva carpeta `cookbook/` con ejemplos Harbour
  ejecutables y muy comentados (simple → avanzado). La pista
  `console/` es xBase puro: crear / seek por índice / transacciones
  / mantenimiento DBF (`ADSCDX`), SQL vía `AdsCreateSQLStatement` +
  `AdsExecuteSQLDirect`, ADT nativo (`ADSADT` + `.adi`) y un cliente
  remoto `tcp://`. La pista `orm/` corre el mismo CRUD vía un ORM
  Harbour compañero sobre back-ends SQLite / DBF / PostgreSQL /
  MariaDB / ODBC, rematada por un benchmark de todos los back-ends
  (`orm/complete/`) con checksum de contenido entre back-ends y
  titular seek-vs-scan. Un ejemplo CRUD `xbrowse` de FiveWin y guías
  de cadenas de conexión / tipos de campo / resolución de problemas
  lo completan.
- **Referencia API (PT)** — las 364 funciones ACE documentadas en
  portugués con sintaxis, parámetros, valores de retorno y ejemplos.

---

## Empaquetado

- **Instalador Windows Inno Setup** (`openads-setup.iss`).
- **Paquetes CPack** con `openace32.lib` / `openace64.lib`
  garantizados en archivos Windows.
- **Release CI** — `openace{32,64}.lib` se incluyen automáticamente
  en archivos de release.

---

## Pruebas

- **874 pruebas unitarias** pasando en x64 y x86 (361 300+
  aserciones).
- Nuevos archivos de prueba: `abi_cdx_tag_order_test.cpp` (orden
  de creación de tags CDX), `abi_cdx_expr_index_scale_test.cpp`
  (tamaño de clave de índice de expresión a escala),
  `abi_multitag_order_nav_test.cpp` (navegación multi-tag),
  `abi_ntx_numeric_edge_test.cpp` (casos límite numéricos NTX),
  `cdx_empty_tree_test.cpp` y más.
- **CI x86** — build leg `msvc-x86` atrapa breakages de 32-bit
  en cada PR.
- Demo de Harbour en `examples/adt-native/` (por glokcode).

---

## Contribuidores

- **Jonsson / RusSoft Ltda.** — correcciones de orden de tags CDX,
  tamaño de clave de índice de expresión, filtro no optimizable de
  `AdsSetAOF` y calling convention x86.
- **Admnwk** — driver ODBC, push-down SQL, agregación,
  FetchWhere V2, ruta de escritura nativa y mejoras de CI.
