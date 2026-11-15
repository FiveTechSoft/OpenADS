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

  MinGW also gets libopenace32.dll.a / libopenace64.dll.a: the same import
  libs bound to the default openace<bits>.dll name instead of the ace<bits>.dll
  drop-in name. The .dll.a suffix keeps them distinct from the static
  libopenace<bits>.a archive, and GNU ld picks libX.dll.a ahead of libX.a in
  the same directory, so -lopenace32 links the DLL when both are present.
  (Linking openace32.dll itself, instead of an import lib, binds 32-bit
  stdcall callers such as rddads to the cdecl AdsXxx aliases - see README.)

  Prereqs (defaults below - override with the -*Bin parameters):
    MSVC      lib.exe / link.exe   (any VS 2022 install, found via vswhere)
    MinGW64   C:\gcc143w64\bin     (dlltool.exe)
    MinGW32   C:\gcc143\bin        (dlltool.exe, ar.exe, objdump.exe)
    Borland64 C:\bcc7764\bin       (mkexp.exe)
    Borland32 C:\bcc77\bin         (implib.exe)

  Inputs: the built engine DLLs
    build/msvc-x64/src/Release/openace64.dll
    build/msvc-x86/src/Release/openace32.dll   (or build-x86, build/release-x86)

  Run from the repo root:
    pwsh dist/import-libs/gen.ps1
  Only the MinGW libs, with other toolchain paths:
    pwsh dist/import-libs/gen.ps1 -Toolchains mingw `
         -MinGW32Bin S:\mingw492\bin -MinGW64Bin C:\mingw64\bin
#>
param(
    [ValidateSet('msvc', 'mingw', 'borland')]
    [string[]]$Toolchains = @('msvc', 'mingw', 'borland'),
    [string]$MinGW32Bin   = 'C:\gcc143\bin',
    [string]$MinGW64Bin   = 'C:\gcc143w64\bin',
    [string]$Borland32Bin = 'C:\bcc77\bin',
    [string]$Borland64Bin = 'C:\bcc7764\bin'
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # repo root
$work = Join-Path $root "build\import-libs"
$out  = Join-Path $PSScriptRoot ""   # commit straight into dist/import-libs

function Require-Tool([string]$path) {
    if (-not (Test-Path $path)) { throw "missing tool: $path" }
    return $path
}

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
if (-not $dll64) { throw "missing openace64.dll - build target openads_ace (x64) first" }
if (-not $dll32) { throw "missing openace32.dll - build target openads_ace (x86) first" }

# MSVC tools: vswhere first (VS installed off C:), then the classic path.
$lib = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -property installationPath
    if ($vs) {
        $lib = (Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe" -ErrorAction SilentlyContinue |
                Select-Object -First 1).FullName
    }
}
if (-not $lib) {
    $lib = (Get-ChildItem "C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC\*\bin\Hostx64\x64\lib.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1).FullName
}
$link = if ($lib) { Join-Path (Split-Path -Parent $lib) "link.exe" } else { $null }
if ($Toolchains -contains 'msvc' -and -not $lib) { throw "lib.exe not found (needed for -Toolchains msvc)" }

# The DLL's real export table. link.exe -dump when MSVC is around, otherwise
# MinGW objdump - both yield the same name list.
function Get-DllExports([string]$dll) {
    if ($link -and (Test-Path $link)) {
        return & $link -dump -exports $dll |
            Select-String '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value }
    }
    $objdump = Require-Tool (Join-Path $MinGW32Bin "objdump.exe")
    $inTable = $false
    $names = foreach ($line in (& $objdump -p $dll)) {
        if ($line -match '\[Ordinal/Name Pointer\] Table') { $inTable = $true; continue }
        if ($inTable) {
            if ($line -match '^\s*\[\s*\d+\]\s+(\S+)\s*$') { $Matches[1] }
            elseif ($line.Trim() -eq '') { $inTable = $false }
        }
    }
    return $names
}

Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory $work | Out-Null
Copy-Item $dll64 "$work\ace64.dll"; Copy-Item $dll32 "$work\ace32.dll"

# .def with an explicit LIBRARY name so the import libs reference ace64/ace32.dll
$def    = Get-Content (Join-Path $root "src\openads_ace.def")
($def    -replace '^\s*LIBRARY\s*$','LIBRARY ace64') | Set-Content "$work\ace64.def" -Encoding ascii

# x86: the DLL exports __stdcall-decorated (_AdsXxx@N) names plus oads_* and
# the legacy-CRT shims. Build the dlltool def from the DLL's real export
# table; a plain-name def would produce import libs whose descriptors point
# at exports that no longer exist, failing every load with 0xC0000139.
$exports32 = Get-DllExports "$work\ace32.dll"
$exports = $exports32 |
    Where-Object { $_ -match '^_(Ads|OAds|oads_)' -or $_ -in @('_dclass','_dsign','_getch','_kbhit','_eof') }
@('LIBRARY ace32', 'EXPORTS') + $exports | Set-Content "$work\ace32.def" -Encoding ascii

# The DLL also exports plain undecorated (AdsXxx) aliases of every entry
# point (see src/abi/ace_stdcall_x86.c). 32-bit MinGW-built Harbour rddads
# references those as cdecl _AdsXxx, so they go into a second def; dlltool's
# default mode prepends the underscore for us. (Reported by Pritpal Bedi.)
$exports_cdecl = $exports32 |
    # AdsXxx (rddads) plus the oads_* VFS API (contrib/oads_hb) — MinGW
    # callers reference both as cdecl _name. (oads_* reported by Pritpal Bedi.)
    Where-Object { $_ -match '^(Ads|OAds|oads_)' }
@('LIBRARY ace32', 'EXPORTS') + $exports_cdecl | Set-Content "$work\ace32_cdecl.def" -Encoding ascii

# Build the merged (stdcall + cdecl) x86 MinGW import lib for one DLL name.
function New-MinGW32ImportLib([string]$dllName, [string]$outLib) {
    $dlltool = Require-Tool (Join-Path $MinGW32Bin "dlltool.exe")
    $ar      = Require-Tool (Join-Path $MinGW32Bin "ar.exe")
    $tag = [IO.Path]::GetFileNameWithoutExtension($dllName)
    # --no-leading-underscore: def names already carry the stdcall
    # decoration (_AdsXxx@N); verified end-to-end against ace32.dll.
    & $dlltool --no-leading-underscore --input-def ace32.def --dllname $dllName --output-lib "${tag}_stdcall.a"
    if ($LASTEXITCODE) { throw "dlltool (stdcall, $dllName) failed" }
    & $dlltool --input-def ace32_cdecl.def --dllname $dllName --output-lib "${tag}_cdecl.a"
    if ($LASTEXITCODE) { throw "dlltool (cdecl, $dllName) failed" }
    # Merge both passes so a single lib serves stdcall (MSVC-style
    # rddads) and cdecl (MinGW rddads) callers alike.
    Remove-Item $outLib -ErrorAction SilentlyContinue
    $arOut = ($outLib -replace '\\','/')
    @"
CREATE $arOut
ADDLIB ${tag}_stdcall.a
ADDLIB ${tag}_cdecl.a
SAVE
END
"@ | & $ar -M
    if ($LASTEXITCODE) { throw "ar merge ($dllName) failed" }
}

Push-Location $work
try {
  if ($Toolchains -contains 'msvc') {
    # The x86 import lib produced by the DLL's own link carries the exact
    # __stdcall-decorated (_AdsXxx@N) symbols 32-bit rddads.lib references.
    $lib32 = Join-Path (Split-Path -Parent $dll32) "openace32.lib"
    if (-not (Test-Path $lib32)) { throw "missing $lib32" }
    & $lib /nologo /def:ace64.def /machine:X64 /out:"$out\x64\msvc\ace64.lib"
    # MSVC x86: the DLL's own import lib carries the __stdcall-decorated
    # (_AdsXxx@N) symbols plus bare-name aliases for the plain exports -
    # but not the _AdsXxx (underscore) form a cdecl x86 consumer references.
    # lib.exe can synthesize those from the plain-name def (it cannot do the
    # @N ones - @N parses as an ordinal), so build a cdecl supplement from
    # ace32_cdecl.def and merge it with the DLL's own lib. (Pritpal Bedi.)
    & $lib /nologo /def:ace32_cdecl.def /machine:X86 /out:ace32_cdecl.lib
    & $lib /nologo /machine:X86 /out:"$out\x86\msvc\ace32.lib" "$lib32" ace32_cdecl.lib
  }
  if ($Toolchains -contains 'mingw') {
    $dlltool64 = Require-Tool (Join-Path $MinGW64Bin "dlltool.exe")
    foreach ($n in @('ace64.dll', 'openace64.dll')) {
        $o = if ($n -eq 'ace64.dll') { "$out\x64\mingw\libace64.a" } else { "$out\x64\mingw\libopenace64.dll.a" }
        & $dlltool64 --input-def ace64.def --dllname $n --output-lib $o
        if ($LASTEXITCODE) { throw "dlltool (x64, $n) failed" }
    }
    New-MinGW32ImportLib 'ace32.dll'     "$out\x86\mingw\libace32.a"
    New-MinGW32ImportLib 'openace32.dll' "$out\x86\mingw\libopenace32.dll.a"
  }
  if ($Toolchains -contains 'borland') {
    & (Require-Tool (Join-Path $Borland64Bin "mkexp.exe"))  "$out\x64\borland\ace64.lib" ace64.dll
    & (Require-Tool (Join-Path $Borland32Bin "implib.exe")) "$out\x86\borland\ace32.lib" ace32.dll
  }
} finally { Pop-Location }

# The MSVC step also drops .exp files next to the .lib - drop them.
Get-ChildItem -Recurse $PSScriptRoot -Filter *.exp | Remove-Item -Force
Write-Output "Regenerated import libs ($($Toolchains -join ', ')) under dist/import-libs."
