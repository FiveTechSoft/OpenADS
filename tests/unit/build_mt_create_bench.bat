@echo off
rem Build mt_create_bench.exe (Harbour -mt) for the ADSCDX vs DBFCDX unit test.
rem Usage: build_mt_create_bench.bat [openace_lib_dir]
rem Default lib dir: ..\..\build\default\src\Release

setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul

set HBROOT=C:\harbour
set OPENADS_LIB=%~1
if "%OPENADS_LIB%"=="" set OPENADS_LIB=%~dp0..\..\build\default\src\Release
set OUTDIR=%~dp0
set PATH=%HBROOT%\bin\win\msvc64;%OPENADS_LIB%;%PATH%

cd /d "%OUTDIR%"
copy /y "%OPENADS_LIB%\openace64.dll" ace64.dll >nul 2>nul
copy /y "%OPENADS_LIB%\openace64.lib" ace64.lib >nul 2>nul
if not exist ace64.dll copy /y "%OPENADS_LIB%\ace64.dll" ace64.dll >nul 2>nul
if not exist ace64.lib copy /y "%OPENADS_LIB%\ace64.lib" ace64.lib >nul 2>nul

hbmk2 -mt -comp=msvc64 -i"%HBROOT%\contrib\rddads" -lrddads -L"%OUTDIR%" -lace64 -llegacy_stdio_definitions -loldnames -omt_create_bench.exe mt_create_bench.prg
if errorlevel 1 exit /b 1
echo Built %OUTDIR%mt_create_bench.exe
endlocal
