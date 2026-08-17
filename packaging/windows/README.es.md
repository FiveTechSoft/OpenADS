# Instalador de Windows (Inno Setup)

`openads-setup.iss` genera un `setup.exe` clásico de Windows para OpenADS — la
puerta de entrada con asistente que los usuarios de Advantage reconocen. Es una
capa gráfica fina sobre **la misma** maquinaria que usa el asistente de consola
multiplataforma (`openads_serverd --setup`): pregunta el directorio de datos, el
puerto, la consola Studio y el arranque automático, escribe un `openads.ini` y
(opcionalmente) registra el servicio de Windows con
`openads_serverd --install-service --config <ini>`. No se duplica lógica de
instalación — Linux/macOS usan el asistente de consola directamente.

## Cómo compilarlo

Necesitas [Inno Setup](https://jrsoftware.org/isinfo.php) (`ISCC.exe`) en una
máquina Windows, más una **carpeta de staging** con los binarios ya compilados
— por ejemplo un zip de release extraído o la salida de `cmake --install` /
`cpack`:

```
ace64.dll  openace64.dll  openads_serverd.exe  openads_bench.exe
openads-studio.bat  openads.ini.sample  QUICKSTART.md
LICENSE  NOTICE  README.md  lib\...
```

Luego:

```bat
iscc /DSrcDir=..\..\dist\openads-1.4.0-windows-x64 /DAppVer=1.4.0 openads-setup.iss
```

- `SrcDir` — ruta a la carpeta de staging (requerida en la práctica; por
  defecto `staging`).
- `AppVer` — versión del instalador; pasa la versión del release para que la
  salida sea `openads-<ver>-setup.exe`. CI debería pasar la etiqueta de git, así
  nada queda fijo en el código (igual que la versión de CMake/CPack).

El `.exe` se genera en `output\`.

## Lo que ve el usuario

1. Licencia + carpeta de instalación (páginas estándar de Inno).
2. **Configuración del servidor** — directorio de datos, puerto (6262 por
   defecto), puerto de Studio, usuario/contraseña de administrador de Studio
   (opcional).
3. **Opciones** — habilitar la consola web Studio; arrancar automáticamente como
   servicio de Windows.
4. Instalar → escribe `openads.ini`, crea la carpeta de datos, registra el
   servicio si se pide. Un acceso directo del menú Inicio lanza **OpenADS
   Studio** (la consola de administración en el navegador).

La desinstalación detiene el servicio primero y luego elimina los archivos.

## Nombres de los ZIP de release

Los assets canónicos de GitHub Release (los genera
`.github/workflows/release.yml` y CPack) son:

- `openads-<ver>-windows-x64.zip`
- `openads-<ver>-windows-x86.zip`

**No** crear ni subir un `openads-<ver>-win-x64.zip` /
`openads-<ver>-win-x86.zip` en paralelo. Ese nombre corto era un kit
manual (cuenta FiveTechSoft, 5 ficheros, otro `ace64.dll`) que en
v1.8.84–v1.8.86 convivía con el ZIP de CI y parecía un duplicado.

Para publicar: empujar el tag `v*` y dejar que CI adjunte los
`windows-*`. No usar `gh release create` / `gh release upload` con un
`*-win-*.zip` local.

## Notas

- El instalador requiere elevación (`PrivilegesRequired=admin`) porque tanto el
  registro del servicio como instalar en Archivos de Programa lo necesitan.
- Este script es **solo para Windows** por naturaleza. No lo construye el flujo
  por defecto de CMake/CPack; añade un paso `iscc` en la rama de release de
  Windows del CI cuando quieras un `setup.exe` firmado junto al zip.
- Los archivos de staging marcados con `skipifsourcedoesntexist` (lanzador de
  Studio, plantilla ini, QUICKSTART) vienen de los PR de onboarding/config; el
  instalador igual compila sin ellos, solo con menos extras.
