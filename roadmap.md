# Roadmap / estado actual

## En curso (2026-08-09) — interop Harbour DBFCDX + dureza multihilo

### Contexto
Serie de reportes de Pritpal Bedi sobre mezcla ADS/DBFCDX (HbDBU, B_BIG).
Releases ya publicadas: **v1.8.65** (visibilidad de índices + guard 5035 +
offsets Harbour VFP + lock de escritura CDX), **v1.8.66** (multi-tag:
AdsCreateIndex ya no trunca el bag; AdsOpenIndex ata todos los tags;
carrera de conteo DBF header-vs-tamaño).

### Lo que hay SIN publicar aún (working tree, pendiente release v1.8.67)
Commit local hecho? NO — cambios sin commitear en:
- `src/drivers/cdx/cdx_index.{cpp,h}` — **fix raíz del GPF "record 151"**
  (`hb_cdxPageSeekKey: wrong parent key`): (a) el descenso de insert
  comparaba solo texto de clave; con duplicados a través de un split hay
  que comparar el par (clave, recno) completo; (b) el separador padre debe
  refrescarse cuando el máximo del hijo crece (cualquier entrada, no solo
  la última); (c) `erase` reescrito recursivo con la misma propagación
  (equivale a NODE_NEWLASTKEY de Harbour).
- `src/abi/ace_exports.cpp` — header DBF Harbour-compatible:
  `32+32n+2` con terminador `0x0D 0x00`; flag de índice de producción
  (byte 28 bit 0x01) cuando el bag comparte basename con la tabla
  (omitido en tablas encriptadas 0xC3/0xC4). Desplazamientos de campo
  (bytes 12-15 de cada descriptor) se MANTIENEN (un reporte anterior
  los exigía; Harbour escribe ceros, ambos son válidos — sizecmp los
  excluye documentadamente).
- Tests nuevos (registrados en `tests/CMakeLists.txt`):
  `cdx_dup_key_split_test.cpp` (decoder independiente + invariante
  separador==máximo-hijo, erase, split-run, RNBits>16383),
  `abi_alternating_append_test.cpp` (alternancia local/local y
  remoto/local estilo B_BIG, orden natural, churn de locks, contención
  cruzada, edición de clave), `abi_mt_contention_test.cpp` (8 writers ×
  50, local y remoto; contención entre hilos; lectores+escritores),
  `cdx_ins_seq_test.cpp`, `abi_ins_order_test.cpp`.
  Smoke: `tests/smoke/harbour/sizecmp.prg` (+ build_run_sizecmp.bat) —
  comparación byte a byte DBF/CDX vs Harbour. PASS actual.
- Fix en `tests/unit/abi_create_table_test.cpp`: hdrLen 32+32n+2.

### BLOQUEANTE — RESUELTO (2026-08-09)
El fallo MT 6106 (`AdsWriteRecord` con 8 hilos) era el registry del
write-lock CDX: otro HILO podía unirse a un batch en curso y leer páginas
que el dueño aún solo tenía en su caché sucia (torn read → 6106). Ahora
cada batch tiene hilo dueño (`CdxWriteLockEntry::owner`); los demás hilos
esperan el flush. 8/8 tests MT en verde; suite x64 completa 100% (5/5).

### Harness de reproducción (C:\OpenADS\_repro1)
- `run.sh`/`run2.sh` — escenario reporte #1 (visibilidad), ambas direcciones.
- `run3.sh` — stress 3 ADS + 3 DBFCDX concurrentes (300/300).
- `run8.sh` — escenario exacto B_BIG (server remoto + DBFCDX, 3 tags,
  duplicados) — 40 instancias limpio.
- `ordcheck.prg` (build10.bat) — validador Harbour de orden (clave,recno).
- `decode_ins.py` — decoder Python de hojas compactas para inspección.

### Pendiente inmediato
1. ~~Resolver el 6106 MT~~ hecho.
2. Suite completa x64 ✅ + **x86 (32 bits)** — en curso al escribir esto
   (`ctest --test-dir build-x86 -C Release`). CI release.yml también
   corre tests x86.
3. Commit + tag v1.8.67 + push → CI genera la release con binarios;
   notas en `_repro1/release-notes-1.8.66.md` como plantilla (escribir
   la de 1.8.67 con el contenido de la entrada de CHANGELOG 1.8.67).
4. Avisar a Pritpal: re-probar su secuencia de 16 instancias con v1.8.67.

### Deudas anotadas
- NTX: sin lock de archivo de índice interproceso (solo DBF alineado).
- Hojas vacías tras erase conservan separador stale-high (lazy delete);
  Harbour haría NODE_JOIN. Riesgo acotado, documentado en el código.
- VFP (0x30) header path sin tocar (hdrLen/displacements estilo FoxPro).
