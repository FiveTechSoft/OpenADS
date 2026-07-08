---
title: Migrando desde ADS
layout: default
parent: Inicio (ES)
nav_order: 1
permalink: /es/migrando-desde-ads/
---

# Migrando desde ADS

OpenADS es un motor open-source compatible con ADS, para aplicaciones creadas
contra el Advantage Database Server (ADS) y la API del Advantage Client Engine
(ACE). Si sus datos son DBF/ADT con índices CDX/NTX/ADI y su app usa `rddads`, la API C de
ACE, el `AXDBFCDX` de X# o la extensión PHP `advantage`, puede migrar sin
reescribir. Esta guía mapea cada pieza de una instalación Advantage a su
equivalente en OpenADS.

## Mapa de conceptos

| Advantage | OpenADS | Notas |
|-----------|---------|-------|
| Advantage Database Server (servicio) | `openads_serverd` | Servidor TCP; se instala como Servicio de Windows / systemd en Linux / launchd en macOS. |
| Advantage Local Server | la propia DLL del motor | `ace64.dll` ejecuta el motor en proceso; sin DLL `adsloc` aparte. |
| `ace32.dll` / `ace64.dll` (cliente) | `ace64.dll` / `ace32.dll` (drop-in) | Mismo nombre, misma ABI C. También como `openace64.dll` para instalaciones lado a lado. |
| Advantage Data Architect (ARC) | consola web **Studio** | UI en navegador: tablas, SQL, estructura, diccionario de datos. Ábrala con `openads-studio.bat`/`.sh`. |
| `ads.cfg` / ajustes | `openads.ini` | Generado por `openads_serverd --setup`, leído con `--config`. |
| Diccionario `.add` / `.ai` | mismos `.add` / `.ai` | Se abren y gestionan de forma nativa. |
| Ruta `\\srv\vol\datos` | `tcp://srv:6262/datos` | URI remota; local sigue siendo ruta de archivo. |
| Puerto TCP por defecto **6262** | **6262** | Idéntico — manténgalo, salvo que ADS siga corriendo en el mismo host (ver abajo). |

## Lado servidor — instale como el setup antiguo

El instalador antiguo de ADS pedía puerto, carpeta de datos, code page y si se
iniciaba como servicio. OpenADS hace lo mismo con un asistente de consola:

```
openads_serverd --setup
```

Escribe un `openads.ini` y, si lo pide, registra el servicio de auto-arranque de
su plataforma. Luego ejecútelo directo desde ese archivo:

```
openads_serverd --config openads.ini
```

> **¿OpenADS y ADS en la misma máquina?** Ambos usan el puerto TCP 6262. Detenga
> el servicio Advantage primero, o asigne otro puerto a OpenADS (`port = 6263` en
> el ini, o `--port 6263`). El servidor avisa claramente si el bind choca.

> **Code page.** OpenADS sirve UTF-8 / CP437 hoy; la selección de code page del
> servidor (CP850, Windows-1252) está en el roadmap. Si sus DBF son CP850, pruebe
> una copia antes de producción.

> **Volver a SAP ACE no es automático.** OpenADS escribe CDX en el formato
> compatible con DBFCDX de Harbour (magic `RCHB` en el offset 0x14 del header
> de la structure tag). SAP ACE no reconoce ese layout y devuelve **error 7017**
> (*Corrupt .ADI, .CDX, or .IDX index*) en cualquier tabla cuyo CDX haya sido
> tocado por OpenADS. OpenADS **lee** CDX construidos por SAP sin modificación,
> siempre que no escriba a través de OpenADS.
>
> Si puede necesitar SAP ACE de nuevo (rollback, entorno mixto o benchmark
> lado a lado), guarde copia de los `.cdx` originales **antes** de apuntar la
> app a OpenADS, o, tras volver a SAP, borre los `.cdx` escritos por OpenADS y
> reconstruya todas las tags bajo SAP. Los índices NTX y ADI siguen la misma
> regla unidireccional tras una escritura de OpenADS.

## Lado cliente — apunte su app a OpenADS

En el caso común **no** relinka:

1. Coloque **`ace64.dll`** (app de 32 bits → `ace32.dll`) junto a su `.exe`, o en
   el `PATH` antes de cualquier copia existente.
2. Ejecute la app. Binarios `rddads` / FiveWin / xBase++ que cargan Advantage por
   el nombre canónico ahora usan OpenADS sin cambios.

Para una compilación nueva, use la import lib de su compilador en `lib/` (MSVC,
MinGW, Borland). Binarios heredados enlazados por ordinal: vea
[`../en/ordinal-compat.md`](../en/ordinal-compat.md). Detalles de RDD:
[`rddads-compat.md`](rddads-compat.md).

Las conexiones remotas usan una URI en lugar de la ruta UNC:

```c
AdsConnect60("tcp://servidor:6262/apps/ventas/ventas.add",
             usuario, clave, ADS_REMOTE_SERVER, &hConn);
```

Otros clientes: la extensión PHP refleja la antigua API `php_advantage`
(`bindings/php_ext/`), y hay un binding FFI portátil en `bindings/php/`.

## Administrar datos — use Studio en lugar de ARC

Ejecute `openads-studio.bat` (Windows) o `./openads-studio.sh` (Linux/macOS) en
la carpeta del release, o, en un servidor en marcha, inícielo con
`--http-port 6263` y abra `http://SERVIDOR:6263/`. Studio cubre lo que hacía en
ARC: navegar y editar registros, ejecutar SQL, ver/reconstruir estructura
(reindex, pack, zap) y crear/editar objetos del diccionario (tablas, usuarios,
índices, reglas de RI). Protéjalo con `--http-user usuario:clave`; para
exposición pública ponga un proxy TLS delante (vea
[`servicio-despliegue.md`](servicio-despliegue.md)).

## Checklist

- [ ] Instalar el servidor: `openads_serverd --setup` → `openads.ini`.
- [ ] Resolver el puerto 6262 si ADS sigue en el host.
- [ ] Dejar `ace64.dll`/`ace32.dll` junto a la app (o relinkar con `lib/`).
- [ ] Guardar copia de los `.cdx` del SAP si puede volver a SAP ACE.
- [ ] Probar lecturas/escrituras/seeks de índice sobre una **copia** de los datos.
- [ ] Verificar acentos (salvedad de code page arriba).
- [ ] Usar Studio (`openads-studio`) en el día a día.
- [ ] Configurar auto-arranque (asistente, o [`servicio-despliegue.md`](servicio-despliegue.md)).

---

*Advantage Database Server, Advantage Client Engine y Advantage Data Architect
son nombres de sus respectivos titulares, citados aquí solo para describir
compatibilidad. OpenADS es un proyecto independiente, sin afiliación ni
respaldo.*
