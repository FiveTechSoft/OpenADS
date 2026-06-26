---
title: Referencia API
layout: default
parent: Inicio (ES)
nav_order: 5
permalink: /es/api-reference/
has_children: true
---

# Referencia API de OpenADS — v1.4.0

Referencia completa de las 357 funciones exportadas `Ads*` en
`ace64.dll` / `ace32.dll` / `libace.so`. Todas las funciones
disponibles para aplicaciones Harbour / X# / Clipper / C / PHP /
.NET.

**Leyenda:**
- ✅ = Completamente implementada
- ⚠️ = Parcial / accept-and-ignore (devuelve `AE_SUCCESS` pero no
  hace nada significativo)
- 🔴 = Stub que devuelve `AE_FUNCTION_NOT_AVAILABLE`
- ➡️ = Reenvío a otra implementación (ej. sobrecarga versionada)

Todas las funciones devuelven `UNSIGNED32` (`AE_SUCCESS` = 0 en
éxito, código de error ACE en fallo) salvo que se indique otra cosa.

---

## Tabla de Contenidos

| # | Categoría | Funciones |
|---|-----------|-----------|
| 1 | [Gestión de Conexiones](#1-gestión-de-conexiones) | 10 |
| 2 | [Operaciones de Tabla](#2-operaciones-de-tabla) | 15 |
| 3 | [Navegación de Registros](#3-navegación-de-registros) | 14 |
| 4 | [Lectura de Campos](#4-lectura-de-campos-por-tipo) | 21 |
| 5 | [Escritura de Campos](#5-escritura-de-campos) | 17 |
| 6 | [Operaciones de Registro](#6-operaciones-de-registro) | 10 |
| 7 | [Bloqueo](#7-bloqueo) | 12 |
| 8 | [Operaciones de Índice](#8-operaciones-de-índice) | 26 |
| 9 | [Seek y Alcance](#9-seek-y-alcance) | 13 |
| 10 | [Filtro y AOF (Rushmore)](#10-filtro-y-aof-rushmore) | 11 |
| 11 | [SQL](#11-sql) | 17 |
| 12 | [Transacciones (TPS)](#12-transacciones-tps) | 8 |
| 13 | [Memo / Binario](#13-memo--binario) | 8 |
| 14 | [Mantenimiento de Tablas](#14-mantenimiento-de-tablas) | 10 |
| 15 | [Cifrado](#15-cifrado) | 10 |
| 16 | [Data Dictionary (DD)](#16-data-dictionary-dd) | 42 |
| 17 | [Evaluación de Expresiones](#17-evaluación-de-expresiones) | 5 |
| 18 | [Telemetría del Servidor (AdsMg*)](#18-telemetría-del-servidor-adsmg) | 17 |
| 19 | [Búsqueda de Texto Completo](#19-búsqueda-de-texto-completo) | 3 |
| 20 | [Misceláneos](#20-misceláneos) | 30 |
| 21 | [Callbacks y Caché](#21-callbacks-y-caché-stubs) | 11 |
| 22 | [Integridad RI y Palancas](#22-integridad-ri-y-palancas) | 7 |
| 23 | [Flush Diferido](#23-flush-diferido) | 2 |
| 24 | [Relaciones (Stubs)](#24-relaciones-stubs) | 3 |
| 25 | [Legacy / Búsqueda](#25-legacy--búsqueda) | 6 |
| | [Resumen](#resumen) | **357** |

---

## 1. Gestión de Conexiones

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsConnect60` | ✅ | Abrir conexión (ruta local, `tcp://`, `tls://`, `sqlite://`, `postgresql://`, `mariadb://`, `mssql://`, `odbc://`) |
| `AdsConnect` | ✅ | Conexión simplificada (sin user/pw/options) |
| `AdsDisconnect` | ✅ | Cerrar conexión y liberar manejadores |
| `AdsGetConnectionType` | ✅ | Devuelve `ADS_LOCAL_SERVER` o `ADS_REMOTE_SERVER` |
| `AdsIsConnectionAlive` | ✅ | Verificación de heartbeat (ping) |
| `AdsResetConnection` | ⚠️ | No-op, devuelve éxito |
| `AdsFindConnection` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — búsqueda por ruta de servidor |
| `AdsFindConnection25` | 🔴 | Sobrecarga versionada (compat X#) |
| `AdsTestLogin` | ⚠️ | Accept-and-ignore |
| `AdsConnect26` | ➡️ | Reenvía a `AdsConnect60` |
| `AdsDisableLocalConnections` | ⚠️ | No-op, devuelve éxito |

---

## 2. Operaciones de Tabla

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsOpenTable` | ✅ | Abrir archivo DBF/CDX/NTX/ADT |
| `AdsCloseTable` | ✅ | Cerrar tabla y liberar recursos |
| `AdsCloseAllTables` | ✅ | Cerrar todas las tablas abiertas |
| `AdsCreateTable` | ✅ | Crear nueva DBF/ADT con definiciones de campo |
| `AdsRestructureTable` | ✅ | Alterar estructura (agregar/eliminar/renombrar campos) |
| `AdsGetTableType` | ✅ | Devuelve `ADS_CDX`, `ADS_NTX`, `ADS_ADT`, etc. |
| `AdsGetTableFilename` | ✅ | Devuelve ruta completa del archivo |
| `AdsGetTableAlias` | ✅ | Devuelve el alias de la tabla |
| `AdsGetTableCharType` | ✅ | Devuelve `ADS_ANSI` o `ADS_OEM` |
| `AdsGetTableConType` | ✅ | Devuelve tipo de conexión de la tabla |
| `AdsGetTableConnection` | ✅ | Devuelve manejador de conexión de la tabla |
| `AdsGetTableOpenOptions` | ✅ | Devuelve flags de modo de apertura |
| `AdsCheckExistence` | ✅ | Verificar si un archivo existe en disco |
| `AdsDeleteFile` | ✅ | Eliminar archivo del directorio de datos |
| `AdsGetNumOpenTables` | ✅ | Devuelve cantidad de tablas abiertas |
| `AdsOpenTable90` | ➡️ | Reenvía a `AdsOpenTable` |
| `AdsCreateTable71` | ➡️ | Reenvía a `AdsCreateTable` |
| `AdsCreateTable90` | ➡️ | Reenvía a `AdsCreateTable` |
| `AdsRestructureTable90` | ➡️ | Reenvía a `AdsRestructureTable` |
| `AdsGetTableHandle25` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` — búsqueda por nombre |

---

## 3. Navegación de Registros

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsGotoTop` | ✅ | Mover al primer registro |
| `AdsGotoBottom` | ✅ | Mover al último registro |
| `AdsGotoRecord` | ✅ | Saltar a un recno específico |
| `AdsSkip` | ✅ | Saltar ±N registros |
| `AdsAtEOF` | ✅ | Verificar si está al final del archivo |
| `AdsAtBOF` | ✅ | Verificar si está al inicio del archivo |
| `AdsIsFound` | ✅ | Verificar si el último `Seek` coincidió |
| `AdsContinue` | ✅ | Continuar un escaneo `LOCATE` |
| `AdsGetRecordNum` | ✅ | Devuelve recno actual |
| `AdsGetRecordCount` | ✅ | Devuelve cantidad total de registros |
| `AdsIsRecordVisible` | ✅ | Verificar si el registro pasa el filtro/AOF |
| `AdsGetBookmark` | ✅ | Obtener marcador de posición (manejador) |
| `AdsGetBookmark60` | ✅ | Obtener marcador como array de bytes |
| `AdsGotoBookmark60` | ✅ | Restaurar posición desde marcador de bytes |

---

## 4. Lectura de Campos (por tipo)

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsGetField` | ✅ | Leer campo como texto (cualquier tipo) |
| `AdsGetFieldW` | ✅ | Leer campo como texto UTF-16 |
| `AdsGetFieldRaw` | ✅ | Leer bytes crudos del disco |
| `AdsGetFieldName` | ✅ | Obtener nombre de campo por ordinal |
| `AdsGetFieldType` | ✅ | Obtener carácter de tipo (C/N/D/L/M/…) |
| `AdsGetFieldLength` | ✅ | Obtener ancho del campo en bytes |
| `AdsGetFieldDecimals` | ✅ | Obtener lugares decimales |
| `AdsGetNumFields` | ✅ | Obtener cantidad de campos |
| `AdsGetString` | ✅ | Leer como cadena |
| `AdsGetStringW` | ✅ | Leer como cadena ancha |
| `AdsGetLong` | ✅ | Leer como entero de 32 bits |
| `AdsGetLongLong` | ✅ | Leer como entero de 64 bits |
| `AdsGetDouble` | ✅ | Leer como double |
| `AdsGetLogical` | ✅ | Leer como booleano (`.T.`/`.F.`) |
| `AdsGetJulian` | ✅ | Leer como Número de Día Juliano |
| `AdsGetDate` | ✅ | Leer como fecha formateada |
| `AdsGetMemoLength` | ✅ | Obtener longitud de datos memo |
| `AdsGetMemoDataType` | ✅ | Obtener tipo de memo (texto/binario) |
| `AdsGetBinaryLength` | ✅ | Obtener longitud de datos binarios |
| `AdsGetBinary` | ✅ | Leer datos binarios |
| `AdsIsNull` | ✅ | Verificar si el campo es NULL |

---

## 5. Escritura de Campos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsSetString` | ✅ | Escribir cadena en campo |
| `AdsSetStringW` | ✅ | Escribir cadena UTF-16 en campo |
| `AdsSetLogical` | ✅ | Escribir valor booleano |
| `AdsSetDouble` | ✅ | Escribir valor double |
| `AdsSetLongLong` | ✅ | Escribir entero de 64 bits |
| `AdsSetJulian` | ✅ | Escribir Número de Día Juliano |
| `AdsSetFieldRaw` | ✅ | Escribir bytes crudos en campo |
| `AdsSetField` | ✅ | Asignador genérico (nombre o ordinal) |
| `AdsSetEmpty` | ✅ | Establecer campo vacío |
| `AdsSetNull` | ✅ | Establecer campo NULL |
| `AdsSetShort` | ✅ | Escribir entero corto |
| `AdsSetMoney` | ✅ | Escribir valor MONEY (64 bits escalado) |
| `AdsSetTime` | ✅ | Escribir valor TIME |
| `AdsSetTimeStamp` | ✅ | Escribir valor TIMESTAMP |
| `AdsSetBinary` | ✅ | Escribir datos binarios |
| `AdsFileToBinary` | ✅ | Importar archivo a campo binario/memo |
| `AdsBinaryToFile` | ✅ | Exportar campo binario/memo a archivo |

---

## 6. Operaciones de Registro

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsAppendRecord` | ✅ | Agregar nuevo registro en blanco |
| `AdsWriteRecord` | ✅ | Volcar registro actual a disco |
| `AdsDeleteRecord` | ✅ | Marcar registro como eliminado |
| `AdsRecallRecord` | ✅ | Restaurar (recuperar) registro |
| `AdsRecallAllRecords` | ⚠️ | No-op, devuelve éxito |
| `AdsIsRecordDeleted` | ✅ | Verificar si el registro está eliminado |
| `AdsIsRecordLocked` | ✅ | Verificar si el registro tiene bloqueo |
| `AdsRefreshRecord` | ✅ | Re-leer registro actual del disco |
| `AdsGetRecordCRC` | ✅ | Calcular CRC-32 del registro actual |
| `AdsWriteAllRecords` | ⚠️ | Devuelve `AE_SUCCESS` (no-op) |

---

## 7. Bloqueo

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsLockRecord` | ✅ | Adquirir bloqueo de rango de bytes |
| `AdsUnlockRecord` | ✅ | Liberar bloqueo de rango de bytes |
| `AdsLockTable` | ✅ | Adquirir bloqueo exclusivo de tabla |
| `AdsUnlockTable` | ✅ | Liberar bloqueo de tabla |
| `AdsGetAllLocks` | ✅ | Obtener array de recnos bloqueados |
| `AdsGetNumLocks` | ✅ | Conteo de bloqueos mantenidos |
| `AdsIsTableLocked` | ✅ | Verificar si la tabla tiene bloqueo exclusivo |
| `AdsTestRecLocks` | ⚠️ | No-op, devuelve éxito |
| `AdsSetLockCycle` | ✅ | Establecer ciclo de escalación de bloqueo |
| `AdsGetLockCycle` | ✅ | Obtener ciclo de escalación de bloqueo |
| `AdsSetLockRetryCount` | ✅ | Establecer cantidad de reintentos de bloqueo |
| `AdsGetLockRetryCount` | ✅ | Obtener cantidad de reintentos de bloqueo |

---

## 8. Operaciones de Índice

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsOpenIndex` | ✅ | Abrir archivo de índice CDX/NTX existente |
| `AdsCloseIndex` | ✅ | Cerrar un índice |
| `AdsCloseAllIndexes` | ✅ | Cerrar todos los índices de una tabla |
| `AdsCreateIndex61` | ✅ | Crear índice CDX/NTX (firma v6.1) |
| `AdsCreateIndex` | ✅ | Crear índice (firma legacy) |
| `AdsDeleteIndex` | ✅ | Eliminar etiqueta de índice |
| `AdsReindex` | ✅ | Reconstruir todos los índices vinculados |
| `AdsGetNumIndexes` | ✅ | Conteo de índices abiertos |
| `AdsGetIndexHandle` | ✅ | Obtener manejador por nombre de etiqueta |
| `AdsGetIndexHandleByOrder` | ✅ | Obtener manejador por posición ordinal |
| `AdsGetIndexExpr` | ✅ | Obtener expresión clave del índice |
| `AdsGetIndexName` | ✅ | Obtener nombre de etiqueta |
| `AdsGetIndexCondition` | ✅ | Obtener condición FOR del índice |
| `AdsGetIndexFilename` | ✅ | Obtener nombre de archivo del índice |
| `AdsGetIndexOrderByHandle` | ✅ | Obtener posición ordinal del manejador |
| `AdsSetIndexOrder` | ✅ | Establecer orden activo por nombre |
| `AdsSetIndexOrderByHandle` | ✅ | Establecer orden activo por manejador |
| `AdsSetIndexDirection` | ✅ | Establecer dirección (ascendente/descendente) |
| `AdsIsIndexCustom` | ✅ | Verificar si el índice es personalizado |
| `AdsIsIndexDescending` | ✅ | Verificar si el índice es descendente |
| `AdsIsIndexUnique` | ✅ | Verificar si el índice es único |
| `AdsAddCustomKey` | ✅ | Agregar clave personalizada |
| `AdsDeleteCustomKey` | ✅ | Eliminar clave personalizada |
| `AdsExtractKey` | ✅ | Extraer clave del registro actual |
| `AdsCreateFTSIndex` | ✅ | Crear índice de búsqueda de texto completo |
| `AdsCreateIndex90` | ➡️ | Reenvía a `AdsCreateIndex61` |
| `AdsReindex61` | ➡️ | Reenvía a `AdsReindex` |

---

## 9. Seek y Alcance

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsSeek` | ✅ | Buscar valor de clave (exacto o suave) |
| `AdsSeekLast` | ✅ | Buscar última clave coincidente |
| `AdsSkipUnique` | ✅ | Saltar a siguiente clave única |
| `AdsSetScope` | ✅ | Establecer alcance de rango de claves |
| `AdsClearScope` | ✅ | Limpiar un alcance |
| `AdsGetScope` | ✅ | Leer alcance actual |
| `AdsClearAllScopes` | ⚠️ | No-op, devuelve éxito |
| `AdsGetKeyNum` | ✅ | Obtener posición relativa de clave (0.0–1.0) |
| `AdsGetKeyCount` | ✅ | Contar claves en orden actual |
| `AdsGetKeyLength` | ✅ | Obtener ancho de clave en bytes |
| `AdsGetKeyType` | ✅ | Obtener tipo de datos de clave |
| `AdsGetRelKeyPos` | ✅ | Obtener posición relativa (fracción) |
| `AdsSetRelKeyPos` | ✅ | Posicionar por fracción relativa |

---

## 10. Filtro y AOF (Rushmore)

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsSetAOF` | ✅ | Instalar filtro optimizado estilo Rushmore |
| `AdsGetAOFOptLevel` | ✅ | Obtener nivel de optimización (FULL/PART/NONE) |
| `AdsClearAOF` | ✅ | Eliminar AOF instalado |
| `AdsRefreshAOF` | ⚠️ | No-op, devuelve éxito |
| `AdsEvalAOF` | ✅ | Evaluar expresión AOF, reportar nivel |
| `AdsGetAOF` | ✅ | Obtener cadena fuente del AOF actual |
| `AdsCustomizeAOF` | ⚠️ | Stub |
| `AdsIsRecordInAOF` | ✅ | Verificar si un registro pasa el AOF |
| `AdsSetFilter` | ⚠️ | No-op (filtro sin índice) |
| `AdsGetFilter` | ✅ | Obtener expresión de filtro actual |
| `AdsClearFilter` | ⚠️ | No-op, devuelve éxito |
| `AdsFilterOption` | ✅ | Obtener opciones de optimización de filtro |

---

## 11. SQL

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsCreateSQLStatement` | ✅ | Asignar manejador de sentencia SQL |
| `AdsCloseSQLStatement` | ✅ | Liberar manejador de sentencia SQL |
| `AdsPrepareSQL` | ✅ | Preparar sentencia SQL |
| `AdsGetNumParams` | ✅ | Obtener cantidad de parámetros |
| `AdsExecuteSQL` | ✅ | Ejecutar SQL preparado, devolver cursor |
| `AdsExecuteSQLDirect` | ✅ | Ejecutar SQL raw, devolver cursor |
| `AdsVerifySQL` | ✅ | Validar sintaxis SQL sin ejecutar |
| `AdsClearSQLParams` | ⚠️ | No-op, devuelve éxito |
| `AdsClearSQLAbortFunc` | ⚠️ | No-op, devuelve éxito |
| `AdsStmtSetTableLockType` | ✅ | Establecer tipo de bloqueo |
| `AdsStmtSetTablePassword` | ✅ | Establecer contraseña por tabla |
| `AdsStmtSetTableReadOnly` | ✅ | Establecer modo solo lectura |
| `AdsStmtSetTableType` | ✅ | Establecer tipo de tabla resultado |
| `AdsStmtSetTableCharType` | ✅ | Establecer tipo de carácter ANSI/OEM |
| `AdsStmtSetTableCollation` | ✅ | Establecer orden de clasificación |
| `AdsStmtSetTableRights` | ✅ | Establecer derechos de acceso |
| `AdsStmtDisableEncryption` | ⚠️ | No-op, devuelve éxito |
| `AdsStmtClearTablePasswords` | ⚠️ | No-op, devuelve éxito |

---

## 12. Transacciones (TPS)

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsBeginTransaction` | ✅ | Iniciar transacción |
| `AdsCommitTransaction` | ✅ | Confirmar transacción actual |
| `AdsRollbackTransaction` | ✅ | Revertir transacción actual |
| `AdsInTransaction` | ✅ | Verificar si está dentro de una transacción |
| `AdsCreateSavepoint` | ✅ | Crear savepoint con nombre |
| `AdsReleaseSavepoint` | ✅ | Liberar savepoint |
| `AdsRollbackTransaction80` | ✅ | Revertir a savepoint (firma ACE 8.0) |
| `AdsFailedTransactionRecovery` | ✅ | Recuperar de transacción fallida |

---

## 13. Memo / Binario

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsGetMemoLength` | ✅ | Obtener longitud de datos memo |
| `AdsGetMemoDataType` | ✅ | Obtener tipo de memo (texto/binario) |
| `AdsGetBinaryLength` | ✅ | Obtener longitud de datos binarios |
| `AdsGetBinary` | ✅ | Leer datos binarios |
| `AdsSetBinary` | ✅ | Escribir datos binarios |
| `AdsBinaryToFile` | ✅ | Exportar memo/binario a archivo |
| `AdsFileToBinary` | ✅ | Importar archivo a campo memo/binario |
| `AdsGetMemoBlockSize` | ✅ | Obtener tamaño de bloque memo |

---

## 14. Mantenimiento de Tablas

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsPackTable` | ✅ | Compactar tabla (eliminar registros eliminados) |
| `AdsZapTable` | ✅ | Vaciar tabla completamente |
| `AdsPackTable_DEFERRED` | ⚠️ | Pack diferido (stub) |
| `AdsZapTable_DEFERRED` | ⚠️ | Zap diferido (stub) |
| `AdsCopyTable` | ✅ | Copiar tabla con filtro opcional |
| `AdsCopyTableContents` | ✅ | Copiar contenidos filtrados a otra tabla |
| `AdsCopyTableContent` | ✅ | Copiar todos los contenidos (alias) |
| `AdsConvertTable` | ✅ | Convertir entre tipos (DBF↔ADT) |
| `AdsCopyTableStructure` | ✅ | Copiar solo estructura (sin datos) |
| `AdsCloneTable` | ✅ | Clonar manejador de tabla (datos compartidos) |

---

## 15. Cifrado

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsEnableEncryption` | ✅ | Habilitar cifrado en conexión |
| `AdsDisableEncryption` | ✅ | Deshabilitar cifrado |
| `AdsIsEncryptionEnabled` | ✅ | Verificar si el cifrado está activo |
| `AdsSetEncryptionPassword` | ✅ | Establecer contraseña de cifrado |
| `AdsIsTableEncrypted` | ✅ | Verificar si la tabla está cifrada |
| `AdsIsRecordEncrypted` | ✅ | Verificar si el registro está cifrado |
| `AdsEncryptTable` | ✅ | Cifrar tabla completa |
| `AdsDecryptTable` | ✅ | Descifrar tabla completa |
| `AdsEncryptRecord` | ✅ | Cifrar registro actual |
| `AdsDecryptRecord` | ✅ | Descifrar registro actual |

---

## 16. Data Dictionary (DD)

### Ciclo de Vida del Diccionario

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreate` | ✅ | Crear nuevo diccionario `.add` |
| `AdsDDAddTable` | ✅ | Registrar alias de tabla |
| `AdsDDRemoveTable` | ✅ | Eliminar alias de tabla |
| `AdsDDAddTable90` | ➡️ | Sobrecarga versionada para X# |

### Propiedades de Tabla

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDGetTableProperty` | ✅ | Leer propiedad de tabla (200–216) |
| `AdsDDSetTableProperty` | ✅ | Escribir propiedad de tabla |
| `AdsDDGetFieldProperty` | ✅ | Leer propiedad de campo (301–309) |
| `AdsDDSetFieldProperty` | ✅ | Escribir propiedad de campo |
| `AdsDDGetIndexProperty` | ✅ | Leer propiedad de índice (401–408) |
| `AdsDDSetIndexProperty` | ⚠️ | Stub |

### Propiedades de Base de Datos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDGetDatabaseProperty` | ✅ | Leer propiedad de BD (1–23) |
| `AdsDDSetDatabaseProperty` | ✅ | Escribir propiedad de BD |

### Usuarios y Grupos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateUser` | ✅ | Crear usuario |
| `AdsDDDeleteUser` | ✅ | Eliminar usuario |
| `AdsDDAddUserToGroup` | ✅ | Agregar usuario a grupo |
| `AdsDDRemoveUserFromGroup` | ✅ | Eliminar usuario de grupo |
| `AdsDDGetUserProperty` | ✅ | Leer propiedad de usuario (1101–1103) |
| `AdsDDSetUserProperty` | ✅ | Escribir propiedad de usuario |

### Permisos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDGetPermissions` | ✅ | Obtener permisos efectivos |
| `AdsDDGrantPermission` | ✅ | Otorgar permiso |
| `AdsDDRevokePermission` | ✅ | Revocar permiso |
| `AdsDDSetUserTableRights` | ✅ | Establecer derechos por tabla |
| `AdsDDGetUserTableRights` | ✅ | Obtener derechos por tabla |

### Gestión de Archivos de Índice

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDAddIndexFile` | ✅ | Vincular archivo de índice a tabla |
| `AdsDDRemoveIndexFile` | ✅ | Desvincular archivo de índice |

### Vistas

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateView` | ✅ | Crear vista SQL con nombre |
| `AdsDDDropView` | ✅ | Eliminar vista |
| `AdsDDAddView` | ✅ | Alias para `AdsDDCreateView` |
| `AdsDDRemoveView` | ✅ | Alias para `AdsDDDropView` |
| `AdsDDGetViewProperty` | ✅ | Leer propiedad de vista (701–702) |
| `AdsDDSetViewProperty` | ✅ | Escribir propiedad de vista |

### Procedimientos Almacenados y Funciones

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateProcedure` | ✅ | Crear procedimiento almacenado |
| `AdsDDDropProcedure` | ✅ | Eliminar procedimiento almacenado |
| `AdsDDAddProcedure` | ✅ | Alias para `AdsDDCreateProcedure` |
| `AdsDDRemoveProcedure` | ✅ | Alias para `AdsDDDropProcedure` |
| `AdsDDGetProcProperty` | ✅ | Leer propiedad (601–605) |
| `AdsDDSetProcProperty` | ✅ | Escribir propiedad |
| `AdsDDGetProcedureProperty` | ✅ | Alias para `AdsDDGetProcProperty` |
| `AdsDDSetProcedureProperty` | ✅ | Alias para `AdsDDSetProcProperty` |
| `AdsDDCreateFunction` | ✅ | Registrar UDF |
| `AdsDDDropFunction` | ✅ | Eliminar UDF |
| `AdsDDGetFunctionProperty` | ✅ | Leer propiedad de UDF |
| `AdsDDSetFunctionProperty` | ✅ | Escribir propiedad de UDF |

### Triggers

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateTrigger` | ✅ | Crear trigger (BEFORE/AFTER/INSTEAD OF) |
| `AdsDDDropTrigger` | ✅ | Eliminar trigger |
| `AdsDDRemoveTrigger` | ✅ | Alias para `AdsDDDropTrigger` |
| `AdsDDGetTriggerProperty` | ✅ | Leer propiedad (501–507) |
| `AdsDDSetTriggerProperty` | ✅ | Escribir propiedad |

### Integridad Referencial

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateRefIntegrity` | ✅ | Crear regla RI (RESTRICT/CASCADE/SETNULL) |
| `AdsDDRemoveRefIntegrity` | ✅ | Eliminar regla RI |
| `AdsDDCreateRefIntegrity62` | ➡️ | Sobrecarga versionada |
| `AdsDDGetRefIntegrityProperty` | ✅ | Leer propiedad RI (401–407) |
| `AdsDDSetRefIntegrityProperty` | ✅ | Escribir propiedad RI |

### Enlaces

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDCreateLink` | ✅ | Crear enlace entre diccionarios |
| `AdsDDDropLink` | ✅ | Eliminar enlace |
| `AdsDDModifyLink` | ✅ | Actualizar credenciales/ruta del enlace |

### Enumeración de Objetos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsDDFindFirstObject` | ✅ | Iniciar iteración por tipo |
| `AdsDDFindNextObject` | ✅ | Continuar iteración |
| `AdsDDFindClose` | ✅ | Cerrar manejador de iteración |

---

## 17. Evaluación de Expresiones

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsEvalLogicalExpr` | ✅ | Evaluar expresión como booleano |
| `AdsEvalNumericExpr` | ✅ | Evaluar expresión como double |
| `AdsEvalStringExpr` | ✅ | Evaluar expresión como cadena |
| `AdsEvalTestExpr` | ⚠️ | Stub |
| `AdsIsExprValid` | ✅ | Validar sintaxis de expresión |

---

## 18. Telemetría del Servidor (AdsMg*)

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsMgConnect` | ✅ | Abrir canal de telemetría |
| `AdsMgDisconnect` | ✅ | Cerrar canal de telemetría |
| `AdsMgGetActivityInfo` | ✅ | Obtener snapshot de actividad |
| `AdsMgGetCommStats` | ✅ | Obtener estadísticas de comunicación |
| `AdsMgGetConfigInfo` | ✅ | Obtener configuración del servidor |
| `AdsMgGetInstallInfo` | ✅ | Obtener info de instalación |
| `AdsMgGetLockOwner` | ✅ | Obtener propietario de un bloqueo |
| `AdsMgGetLocks` | ✅ | Listar todos los bloqueos |
| `AdsMgGetOpenIndexes` | ✅ | Listar índices abiertos |
| `AdsMgGetOpenTables` | ✅ | Listar tablas abiertas |
| `AdsMgGetOpenTables2` | ✅ | Lista extendida de tablas |
| `AdsMgGetServerType` | ✅ | Obtener tipo de servidor |
| `AdsMgGetUserNames` | ✅ | Listar usuarios conectados |
| `AdsMgGetWorkerThreadActivity` | ✅ | Obtener info de hilos |
| `AdsMgKillUser` | ✅ | Desconectar usuario |
| `AdsMgResetCommStats` | ✅ | Reiniciar contadores de comunicación |
| `AdsMgDumpInternalTables` | ✅ | Volcar metadata de tablas internas |

---

## 19. Búsqueda de Texto Completo

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsCreateFTSIndex` | ✅ | Crear índice FTS en un campo |
| `AdsFTSSearch` | ✅ | Buscar en índice FTS con consulta |
| `AdsGetFTSIndexes` | ⚠️ | Stub |

---

## 20. Misceláneos

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsGetVersion` | ✅ | Obtener versión ACE (major, minor, letter, desc) |
| `AdsGetLastError` | ✅ | Obtener último código de error y mensaje |
| `AdsGetErrorString` | ✅ | Obtener cadena legible del error |
| `AdsGetServerName` | ✅ | Obtener nombre del servidor |
| `AdsGetServerTime` | ✅ | Obtener timestamp del servidor |
| `AdsGetDateFormat` | ✅ | Obtener formato de fecha del proceso |
| `AdsSetDateFormat` | ✅ | Establecer formato de fecha del proceso |
| `AdsGetLastTableUpdate` | ✅ | Obtener fecha de última actualización |
| `AdsGetLastAutoinc` | ✅ | Obtener último valor autoincrement |
| `AdsShowDeleted` | ✅ | Alternar visibilidad `SET DELETED` |
| `AdsGetDeleted` | ✅ | Consultar estado `SET DELETED` |
| `AdsSetCollation` | ✅ | Establecer orden de clasificación |
| `AdsConvertOemToAnsi` | ✅ | Conversión OEM→ANSI |
| `AdsConvertAnsiToOem` | ✅ | Conversión ANSI→OEM |
| `AdsGetEpoch` | ✅ | Obtener pivot de año de 2 dígitos |
| `AdsSetEpoch` | ⚠️ | No-op, devuelve éxito |
| `AdsGetExact` | ✅ | Obtener estado `SET EXACT` |
| `AdsSetExact` | ⚠️ | No-op, devuelve éxito |
| `AdsGetDefault` | ✅ | Obtener unidad/ruta por defecto |
| `AdsSetDefault` | ⚠️ | No-op, devuelve éxito |
| `AdsGetSearchPath` | ✅ | Obtener ruta de búsqueda |
| `AdsSetSearchPath` | ⚠️ | No-op, devuelve éxito |
| `AdsGetNumActiveLinks` | ✅ | Contar enlaces activos |
| `AdsGetNumOpenTables` | ✅ | Contar tablas abiertas |
| `AdsApplicationExit` | ⚠️ | No-op, devuelve éxito |
| `AdsThreadExit` | ⚠️ | No-op, devuelve éxito |
| `AdsInitRawKey` | ⚠️ | No-op, devuelve éxito |
| `AdsGetRecord` | ⚠️ | Stub |
| `AdsSetRecord` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsGetMilliseconds` | ⚠️ | Stub |
| `AdsSetMilliseconds` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsData` | ⚠️ | No-op, devuelve éxito |

---

## 21. Callbacks y Caché (Stubs)

Estas funciones se aceptan por compatibilidad ABI pero no tienen
efecto en OpenADS:

| Función | Devuelve |
|---------|----------|
| `AdsRegisterCallbackFunction` | `AE_SUCCESS` |
| `AdsRegisterProgressCallback` | `AE_SUCCESS` |
| `AdsClearCallbackFunction` | `AE_SUCCESS` |
| `AdsClearProgressCallback` | `AE_SUCCESS` |
| `AdsCacheOpenCursors` | `AE_SUCCESS` |
| `AdsCacheOpenTables` | `AE_SUCCESS` |
| `AdsCacheRecords` | `AE_SUCCESS` |
| `AdsCloseCachedTables` | `AE_SUCCESS` |
| `AdsSetDecimals` | `AE_SUCCESS` |
| `AdsShowError` | `AE_SUCCESS` |
| `AdsSetServerType` | `AE_SUCCESS` |

---

## 22. Integridad RI y Palancas

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsEnableRI` | ⚠️ | No-op, devuelve éxito |
| `AdsDisableRI` | ⚠️ | No-op, devuelve éxito |
| `AdsEnableUniqueEnforcement` | ⚠️ | No-op |
| `AdsDisableUniqueEnforcement` | ⚠️ | No-op |
| `AdsEnableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsDisableAutoIncEnforcement` | ⚠️ | No-op |
| `AdsCancelUpdate` | ⚠️ | No-op |

---

## 23. Flush Diferido

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsSetDeferredFlush` | ✅ | Alternar flush diferido (528× inserción masiva) |
| `AdsFlushFileBuffers` | ✅ | Forzar fsync en archivos de tabla + índice |

---

## 24. Relaciones (Stubs)

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsSetRelation` | 🔴 | `AE_FUNCTION_NOT_AVAILABLE` |
| `AdsSetScopedRelation` | ⚠️ | No-op, devuelve éxito |
| `AdsClearRelation` | ⚠️ | No-op, devuelve éxito |

---

## 25. Legacy / Búsqueda

| Función | Estado | Descripción |
|---------|--------|-------------|
| `AdsFindFirstTable` | ✅ | Encontrar primera tabla que coincida |
| `AdsFindNextTable` | ✅ | Encontrar siguiente tabla |
| `AdsFindClose` | ✅ | Cerrar manejador de búsqueda |
| `AdsFindFirstTable62` | ➡️ | Sobrecarga versionada |
| `AdsFindNextTable62` | ➡️ | Sobrecarga versionada |
| `AdsIsServerLoaded` | ✅ | Verificar si el servidor está local |

---

## Resumen

| Categoría | Total | ✅ | ⚠️ | 🔴 |
|-----------|------:|----:|----:|----:|
| Conexiones | 11 | 7 | 2 | 2 |
| Tabla | 20 | 15 | 2 | 3 |
| Navegación | 14 | 14 | 0 | 0 |
| Lectura | 21 | 21 | 0 | 0 |
| Escritura | 17 | 17 | 0 | 0 |
| Registro | 10 | 8 | 2 | 0 |
| Bloqueo | 12 | 10 | 2 | 0 |
| Índice | 27 | 25 | 0 | 2 |
| Seek | 13 | 12 | 1 | 0 |
| Filtro/AOF | 12 | 8 | 4 | 0 |
| SQL | 18 | 12 | 6 | 0 |
| Transacción | 8 | 8 | 0 | 0 |
| Memo/Binario | 8 | 8 | 0 | 0 |
| Mantenimiento | 10 | 8 | 2 | 0 |
| Cifrado | 10 | 10 | 0 | 0 |
| Data Dictionary | 42 | 40 | 2 | 0 |
| Expresiones | 5 | 4 | 1 | 0 |
| Telemetría | 17 | 17 | 0 | 0 |
| Texto Completo | 3 | 2 | 1 | 0 |
| Misceláneos | 31 | 18 | 11 | 2 |
| Callbacks/Caché | 11 | 0 | 11 | 0 |
| Palancas RI | 7 | 0 | 7 | 0 |
| Flush Diferido | 2 | 2 | 0 | 0 |
| Relaciones | 3 | 0 | 2 | 1 |
| Legacy | 6 | 4 | 0 | 2 |
| **TOTAL** | **357** | **~250** | **~56** | **~12** |
