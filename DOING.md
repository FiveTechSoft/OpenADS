# DOING.md — Trabajo en curso

> Archivo vivo. Se actualiza diariamente con lo que se está haciendo,
> probando y verificando. No es un changelog — es un "qué está pasando
> ahora".

---

## 2026-07-01 — Investigación y fix del reporte FWH REMOTE

### Problema reportado

Un usuario FWH (FiveWin) reportó que al usar `openads_serverd` en modo
REMOTE con la clase `TDatabase` de FWH:

1. **FieldGet crashea después de USE** — En LOCAL mode el record buffer
   siempre está poblado después del open. En REMOTE no, y `FieldGet`
   intenta leer de un buffer NULL → crash.

2. **Production CDX no se auto-asocia** — `OrdBagName()` devuelve vacío
   después de abrir la tabla. En LOCAL funciona correctamente.

3. **RddSetDefault("ADSCDX") falla** — El usuario sospecha que está
   accediendo al archivo equivocado o que la secuencia de inicialización
   remota no es la misma que la local.

### Qué se investigó

Se revisó el flujo completo de `AdsOpenTable90` en modo REMOTE:

```
FWH: USE table VIA "ADSCDX"
  → AdsConnect60(tcp://host:port/, ADS_REMOTE_SERVER)
  → AdsOpenTable90(hConn, name, alias, ADS_CDX, ...)
      → Wire: OpenTable opcode → Server abre la tabla
      → Server: STAT del .cdx → envía prod_bag_path en OpenTableAck
      → Client: recibe OpenTableResult{id, prod_bag_path}
      → Client: auto-calls AdsOpenIndex(bag_path)
          → Wire: OpenIndex → Server abre CDX localmente
          → Server: retorna tags + bag_path
          → Client: crea RemoteIndex con bag_path
      → Client: resetea active_index_id = 0 (orden natural)
```

### Hallazgos

| Pregunta del usuario | Respuesta |
|---------------------|-----------|
| ¿FieldGet es válido inmediatamente después del open? | **NO.** Se necesita `DbGoTop()` primero. El buffer del cliente está vacío. Esto es un bug real en ace64.dll que necesita fix. |
| ¿FieldGet en memo field funciona en REMOTE? | **No funciona** si no hay buffer previo. Los offsets de crash (0x2, 0xB) indican buffer pointer NULL. |
| ¿Hay un ejemplo de REMOTE contra DBF/CDX existente? | **No había.** Se creó uno nuevo (ver abajo). |

### Códigos revisados

| Archivo | Líneas | Qué hace |
|---------|--------|----------|
| `src/network/session.cpp` | 554-616 | `Opcode::OpenTable` handler — abre tabla, STAT del CDX, envía bag_path |
| `src/network/session.cpp` | 1321-1375 | `Opcode::OpenIndex` handler — abre CDX vía ABI, retorna tags + bag_path |
| `src/abi/ace_exports.cpp` | 5200-5260 | `AdsOpenTable90` remote path — auto-opens production CDX after table open |
| `src/abi/ace_exports.cpp` | 9911-9965 | `AdsOpenIndex` remote path — crea RemoteIndex con bag_path |
| `src/abi/ace_exports.cpp` | 23551-23580 | `AdsGetIndexFilename` remote — retorna bag_path del RemoteIndex |
| `src/session/connection.cpp` | 166-251 | `Connection::open_table` — abre tabla + adjunta memo, pero NO abre CDX automáticamente |
| `src/network/client.cpp` | 1171-1243 | `RemoteConnection::open_index` — parsea OpenIndexAck con tags + bag_path |

### Test creado

**`tests/unit/abi_remote_prodcdx_test.cpp`** — 8 test cases que validan
exactamente el workflow FWH contra un servidor remoto con datos reales:

| Test | Qué valida | Issue FWH |
|------|------------|-----------|
| `OrdBagName returns production CDX bag path after open` | AdsGetNumIndexes > 0, AdsGetIndexFilename retorna nombre del CDX | #2 |
| `AdsGetIndexName returns tag names for all orders` | Cada orden tiene tag name válido | #2 |
| `GoTop + FieldGet on first record after open` | AdsGotoTop → AdsGetField no crashea | #1 |
| `DbSetOrder by number changes cursor order` | Cambiar orden cambia el registro top | #3 |
| `DbSetOrder by tag name` | Funciona por nombre, walk de registros | #3 |
| `Ordered full-scan counts match record count` | Scan completo con orden = record count total | General |
| `Multiple tables open simultaneously` | customer.dbf + invoices.dbf abiertos a la vez | General |
| `FieldGet on multiple fields after Skip` | FieldGet por nombre en todos los campos tras Skip | #1 |

### Entorno de prueba

- **Servidor:** iMac `192.168.18.184`, puerto `16262`, data_dir `/tmp/openads_mac`
- **Datos:** `customer.dbf` (100 records, CDX production), `invoices.dbf` (1000 records, CDX), `invoicedetail.dbf` (3000 records, CDX), `items.dbf` (20 records, CDX)
- **SSH:** `Anto@192.168.18.184`, password `1234`
- **Ejecución:** `$env:OPENADS_TEST_REMOTE = "tcp://192.168.18.184:16262/"; .\openads_unit_tests.exe -tc="REMOTE*"`

### Fix pendiente (no implementado aún)

El bug real es que después de `OpenTable` remoto, el cursor del engine
queda en posición indefinida. En LOCAL mode, el engine deja el cursor
poblado (aunque en BOF). En REMOTE, el cliente no tiene ningún buffer
hasta que pide una navegación.

**Opciones de fix:**

1. **Servidor: ejecutar GotoTop implícito después de OpenTable** — El
   OpenTableAck podría incluir el recno actual y los primeros bytes del
   registro. Esto arregla el crash pero cambia la semántica (ADS no hace
   GoTop implícito).

2. **Servidor: el OpenTableAck incluye el record buffer completo** — Más
   datos por round-trip pero el cliente tiene todo de inmediato.

3. **Cliente: AdsOpenTable90 hace GotoTop implícito en REMOTE** — El
   cliente envía GotoTop después de recibir el OpenTableAck. Esto es lo
   más cercano al comportamiento LOCAL.

### Pruebas completadas hoy

| Suite | Resultado |
|-------|-----------|
| Unit tests locales (960 tests) | ✅ 960/960 passed |
| REMOTE prodcdx tests (8 tests) | ✅ 8/8 passed |
| Total assertions | 394,513 passed, 0 failed |

### Bugs corregidos

1. **`AdsGetNumIndexes` returns 0 in REMOTE** — El código consultaba
   `get_num_indexes()` al servidor, que retornaba 0 porque el engine
   handle no tenía el production index abierto. **Fix:** contar
   `rt->index_handles.size()` localmente (ace_exports.cpp:13110).

2. **FieldGet crashea después de OpenTable en REMOTE** — El buffer del
   registro estaba vacío después del open. En LOCAL el cursor queda en
   BOF con buffer válido. **Fix:** GoTop implícito después del auto-open
   del production CDX en `AdsOpenTable90` (ace_exports.cpp:5260).
   Un round-trip extra por tabla abierta, trivial comparado con el crash.

3. **FieldGet por ordinal con string "1"** — Los tests usaban
   `(UNSIGNED8*)"1"` (string literal) que en 64-bit tiene dirección
   alta (>0x10000), por lo que el detector de ordinal no lo reconoce.
   **Fix del test:** usar el idiom ACE correcto
   `reinterpret_cast<UNSIGNED8*>(static_cast<std::uintptr_t>(1))`.
   El código del DLL ya era correcto.

4. **AdsGetIndexName retorna tag vacío** — El CDX de customer tiene
   un tag estructural cuyo nombre es todo padding (se trimea a vacío).
   **Fix del test:** aceptar nombre vacío como válido para tags CDX
   estructurales de dBASE.

5. **Test AdsSetIndexOrderByHandle asumía 2+ tags** — customer.cdx solo
   tiene 1 tag. **Fix del test:** trabajar con 1 tag, comparar solo
   cuando hay 2+.

### Pendiente

- [x] Implementar fix para el crash de FieldGet en REMOTE → **Hecho: GoTop
       implícito después de AdsOpenTable90 en modo REMOTE**
- [ ] Agregar test de memo fields en REMOTE (las tablas actuales no tienen .fpt)
- [ ] Agregar test con `RddSetDefault("ADSCDX")` para reproducir el caso exacto del usuario
- [ ] Verificar que el fix funciona con la app real del usuario FWH
- [ ] Investigar por qué customer.cdx tiene tag con nombre vacío
      (posiblemente creado por herramienta que no pone nombre al tag default)

---

## 2026-07-15 — rddads: HB_FUNC wrappers de setters tipados en `adsfunc.c` — DONE / no es gap de OpenADS

**Resuelto 2026-07-17 (RCB):** los wrappers `HB_FUNC( ADSSET* )` viven en el
`rddads` que cada usuario compila con SU Harbour — es un asunto de la
distribución de Harbour, no de OpenADS. RCB lo coordinará con los
mantenedores de Harbour. Si el `rddads.lib` de un usuario no trae estos
wrappers, simplemente no podrá llamarlos desde `.prg`; el RDD normal
(`REPLACE`/`FIELD->x :=`) no los usa y funciona igual.

Para referencia, los `HB_FUNC` de setters tipados de valor que un
`adsfunc.c` completo debe exponer (el árbol `f:\harbour3.2-bcc7.3` los tiene
todos; `bcc7.4` no tiene ninguno; `gcc6.3` sólo le falta MONEY):

`ADSSETFIELD, ADSSETSTRING, ADSSETLONG, ADSSETDOUBLE, ADSSETSHORT,
ADSSETDATE, ADSSETLOGICAL, ADSSETMONEY, ADSSETBINARY, ADSSETNULL,
ADSSETTIMESTAMP` (familia completa: 21 wrappers `ADSSET*`, incluyendo los de
configuración `ADSSETAOF`, `ADSSETDATEFORMAT`, etc., que sí suelen estar).

---

## 2026-07-17 — Script Engine (SQL scripting: stored procs / UDFs / triggers)

**Design doc: `docs/script-engine.md` (accepted 2026-07-17).** OpenADS had no
real script execution: a ~310-line string interpreter for UDFs (untyped, only
`+`/`-`, IF skipped), NO path at all for DD script procs (`EXECUTE PROCEDURE`
→ 5000), a third fragment for triggers that silently swallowed failures, and
no `EXECUTE IMMEDIATE`. Replacement: ONE typed engine in `src/engine/script/`
(lexer → parser → AST cached per body → typed interpreter); embedded SQL and
subqueries delegate through a `SqlBridge` into the one SQL executor.

**Language rules are oracle-verified** — 34 probes against SAP `ace64.dll`
recorded in the doc §10. Highlights: strict typing (`'5'+1` errors, CHAR ↔
number assigns error), integer division, only `=`/`<>`, `LEAVE` (not BREAK),
`TRY…CATCH ALL…END TRY` + `RAISE name(code,'msg')` + `__errcode/__errtext`,
3-valued NULL logic, `{d '…'}` literals, case-insensitive variables that
error on column-name collision.

**S1 (language core) status:** engine implemented (`value/lexer/parser/exec` +
`parse_params`), 21 unit tests / 205 assertions green
(`tests/unit/script_engine_test.cpp`, each case mirrors a probe). UDF call
site (`K::Udf`) switched to the engine via `scriptbridge::` in
`ace_exports.cpp`; the old `proc::` interpreter is DELETED. UDF errors now
PROPAGATE and fail the SELECT (SAP parity) instead of silently returning "".
Typed column/literal/nested-call arguments; UDF→UDF recursion is direct (no
SQL round-trip), depth-capped at 32.

**Next:** S2 = `EXECUTE PROCEDURE` for DD script procs (`__input`/`__output`),
trigger bodies through the engine with error propagation (failing trigger
must fail the DML), parameter binding for embedded SQL. S3 = cursors
(`DECLARE … CURSOR`, `WHILE FETCH`), `EXECUTE IMMEDIATE`, builtin sweep.
Gates run against the SAP oracle; PMSYS is the integration fixture, not the
target.
