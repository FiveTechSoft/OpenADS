<#
  Regenerate the per-compiler import libraries for the OpenADS engine DLL.

  These let applications built with MSVC, MinGW/GCC, or Borland/C++Builder
  link against ace64.dll / ace32.dll by name. They are committed because the
  release CI runners do not have the Borland toolchain; regenerate them here
  whenever src/openads_ace.def or src/openads_ace_x86.def changes, then
  commit the result.

  x86 and x64 import libs both use undecorated (__cdecl) names so
  hbmk2 / MinGW projects link without @N stdcall-decoration mismatches.
  See openads_ace_x86.def for details.

  Prereqs (paths below — adjust if your install differs):
    MSVC      lib.exe   (any VS 2022 install)
    MinGW64   C:\gcc143w64\bin\dlltool.exe
    MinGW32   C:\gcc143\bin\dlltool.exe
    Borland64 C:\bcc7764\bin\mkexp.exe
    Borland32 C:\bcc77\bin\implib.exe

  Inputs: the built engine DLLs
    build/msvc-x64/src/Release/openace64.dll
    build/release-x86/src/Release/openace32.dll

  Run from the repo root:  pwsh dist/import-libs/gen.ps1
#>
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # repo root
$work = Join-Path $root "build\import-libs"
$out  = Join-Path $PSScriptRoot ""   # commit straight into dist/import-libs

$dll64 = Join-Path $root "build\msvc-x64\src\Release\openace64.dll"
$dll32 = Join-Path $root "build\release-x86\src\Release\openace32.dll"
if (-not (Test-Path $dll64)) { throw "missing $dll64 — build target openads_ace (x64) first" }
if (-not (Test-Path $dll32)) { throw "missing $dll32 — build target openads_ace (x86) first" }

$lib = (Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe" |
        Select-Object -First 1).FullName

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
Copy-Item $dll64 "$work\ace64.dll"; Copy-Item $dll32 "$work\ace32.dll"

# .def with an explicit LIBRARY name so the import libs reference ace64/ace32.dll
$def    = Get-Content (Join-Path $root "src\openads_ace.def")
$defX86 = Get-Content (Join-Path $root "src\openads_ace_x86.def")
($def    -replace '^\s*LIBRARY\s*$','LIBRARY ace64') | Set-Content "$work\ace64.def" -Encoding ascii
($defX86 -replace '^\s*LIBRARY\s*$','LIBRARY ace32') | Set-Content "$work\ace32.def" -Encoding ascii

Push-Location $work
try {
  & $lib /nologo /def:ace64.def /machine:X64 /out:"$out\x64\msvc\ace64.lib"
  & $lib /nologo /def:ace32.def /machine:X86 /out:"$out\x86\msvc\ace32.lib"
  & "C:\gcc143w64\bin\dlltool.exe" --input-def ace64.def --dllname ace64.dll --output-lib "$out\x64\mingw\libace64.a"
  & "C:\gcc143\bin\dlltool.exe"    --input-def ace32.def --dllname ace32.dll --output-lib "$out\x86\mingw\libace32.a"
  & "C:\bcc7764\bin\mkexp.exe"  "$out\x64\borland\ace64.lib" ace64.dll
  & "C:\bcc77\bin\implib.exe"   "$out\x86\borland\ace32.lib" ace32.dll
} finally { Pop-Location }

# The MSVC step also drops .exp files next to the .lib — drop them.
Get-ChildItem -Recurse $PSScriptRoot -Filter *.exp | Remove-Item -Force
Write-Output "Regenerated import libs under dist/import-libs."
