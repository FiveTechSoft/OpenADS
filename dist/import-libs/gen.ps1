<#
  Regenerate the per-compiler import libraries for the OpenADS engine DLL.

  These let applications built with MSVC, MinGW/GCC, or Borland/C++Builder
  link against ace64.dll / ace32.dll by name. They are committed because the
  release CI runners do not have the Borland toolchain; regenerate them here
  whenever src/openads_ace.def or src/abi/ace_stdcall_x86.c changes, then
  commit the result.

  x64 import libs use undecorated (__cdecl) names (x64 has a single
  calling convention). The x86 DLL exports __stdcall-decorated
  (_AdsXxx@N) names - the MSVC x86 import lib is copied from the DLL's
  own link byproduct, MinGW's is generated with dlltool
  --no-leading-underscore, Borland's straight from the DLL.
  See src/openads_ace_x86_stdcall.def and src/abi/ace_stdcall_x86.c.

  Prereqs (paths below - adjust if your install differs):
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

$dll64 = $null
foreach ($cand in @("build\msvc-x64", "build\default")) {
    $p = Join-Path $root "$cand\src\Release\openace64.dll"
    if ((Test-Path $p) -and
        (-not $dll64 -or (Get-Item $p).LastWriteTime -gt (Get-Item $dll64).LastWriteTime)) {
        $dll64 = $p
    }
}
$dll32 = $null
foreach ($cand in @("build\msvc-x86", "build-x86", "build\release-x86")) {
    $p = Join-Path $root "$cand\src\Release\openace32.dll"
    if ((Test-Path $p) -and
        (-not $dll32 -or (Get-Item $p).LastWriteTime -gt (Get-Item $dll32).LastWriteTime)) {
        $dll32 = $p
    }
}
if (-not (Test-Path $dll64)) { throw "missing $dll64 - build target openads_ace (x64) first" }
if (-not $dll32) { throw "missing openace32.dll - build target openads_ace (x86) first" }
# The x86 import lib produced by the DLL's own link carries the exact
# __stdcall-decorated (_AdsXxx@N) symbols 32-bit rddads.lib references.
$lib32 = Join-Path (Split-Path -Parent $dll32) "openace32.lib"
if (-not (Test-Path $lib32)) { throw "missing $lib32" }

$lib = (Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe" |
        Select-Object -First 1).FullName

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
Copy-Item $dll64 "$work\ace64.dll"; Copy-Item $dll32 "$work\ace32.dll"

# .def with an explicit LIBRARY name so the import libs reference ace64/ace32.dll
$def    = Get-Content (Join-Path $root "src\openads_ace.def")
($def    -replace '^\s*LIBRARY\s*$','LIBRARY ace64') | Set-Content "$work\ace64.def" -Encoding ascii

# x86: the DLL exports __stdcall-decorated (_AdsXxx@N) names plus oads_* and
# the legacy-CRT shims. Build the dlltool def from the DLL's real export
# table (link.exe -dump sits next to lib.exe); a plain-name def would
# produce import libs whose descriptors point at exports that no longer
# exist, failing every load with 0xC0000139.
$link = Join-Path (Split-Path -Parent $lib) "link.exe"
$exports = & $link -dump -exports "$work\ace32.dll" |
    Select-String '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)' |
    ForEach-Object { $_.Matches[0].Groups[1].Value } |
    Where-Object { $_ -match '^_(Ads|oads_)' -or $_ -in @('_dclass','_dsign','_getch','_kbhit','_eof') }
@('LIBRARY ace32', 'EXPORTS') + $exports | Set-Content "$work\ace32.def" -Encoding ascii

Push-Location $work
try {
  & $lib /nologo /def:ace64.def /machine:X64 /out:"$out\x64\msvc\ace64.lib"
  # MSVC x86: the DLL's own import lib already has the right _AdsXxx@N
  # symbols - lib.exe cannot synthesize them from a def (@N parses as an
  # ordinal), so copy it instead.
  Copy-Item $lib32 "$out\x86\msvc\ace32.lib" -Force
  & "C:\gcc143w64\bin\dlltool.exe" --input-def ace64.def --dllname ace64.dll --output-lib "$out\x64\mingw\libace64.a"
  # --no-leading-underscore: def names already carry the stdcall
  # decoration (_AdsXxx@N); verified end-to-end against ace32.dll.
  & "C:\gcc143\bin\dlltool.exe"    --no-leading-underscore --input-def ace32.def --dllname ace32.dll --output-lib "$out\x86\mingw\libace32.a"
  & "C:\bcc7764\bin\mkexp.exe"  "$out\x64\borland\ace64.lib" ace64.dll
  & "C:\bcc77\bin\implib.exe"   "$out\x86\borland\ace32.lib" ace32.dll
} finally { Pop-Location }

# The MSVC step also drops .exp files next to the .lib - drop them.
Get-ChildItem -Recurse $PSScriptRoot -Filter *.exp | Remove-Item -Force
Write-Output "Regenerated import libs under dist/import-libs."
