@echo off
rem ===================================================================
rem  qa-diff -- differential QA: same xBase ops on native DBF vs OpenADS.
rem  No baked-in paths: the OpenADS lib folder comes from the argument.
rem
rem  Usage:  run.cmd <openads-lib-dir>
rem     <openads-lib-dir>  folder holding openace64.dll + its import lib
rem
rem  Prereqs: MSVC x64 dev prompt (cl/link on PATH), hbmk2 on PATH or HB_BIN,
rem           Harbour with the rddads contrib.
rem  Env (optional): HB_BIN, OPENADS_ACELIB (default openace64).
rem
rem  Builds qamatrix once, runs it for DBFCDX/DBFNTX/ADSCDX/ADSNTX, then
rem  diffs native (oracle) vs ADS. Any differing line = candidate bug.
rem ===================================================================
setlocal EnableDelayedExpansion
if "%~1"=="" ( echo Usage: run.cmd ^<openads-lib-dir^> & exit /b 2 )
set "OPENADS_LIB=%~1"
if not defined OPENADS_ACELIB set "OPENADS_ACELIB=openace64"
set "HBMK=hbmk2"
if defined HB_BIN set "HBMK=%HB_BIN%"
set "HBMK2="
set "HB_USER_OPTIONS="
set "PATH=%OPENADS_LIB%;%PATH%"
pushd "%~dp0"

rem CRT-compat flags for Harbour libs built against an older VC runtime.
set "CRTFIX=-ldflag=/NODEFAULTLIB:msvcrt -ldflag=/NODEFAULTLIB:libucrt -ldflag=/NODEFAULTLIB:libvcruntime.lib -ldflag=/FORCE:MULTIPLE -lucrt -lvcruntime -llegacy_stdio_definitions"

echo === build qamatrix ===
"%HBMK%" qamatrix.hbp -comp=msvc64 -cpu=x86_64 -static -gtstd %CRTFIX%
if errorlevel 1 ( echo BUILD FAILED & popd & exit /b 1 )
copy /y "%OPENADS_LIB%\%OPENADS_ACELIB%.dll" . >nul 2>&1

for %%R in (DBFCDX DBFNTX ADSCDX ADSNTX) do (
   echo === run %%R ===
   ".\qamatrix.exe" %%R qa_%%R.log
)

echo.
echo === DIFF DBFCDX (oracle) vs ADSCDX (OpenADS) ===
fc qa_DBFCDX.log qa_ADSCDX.log
echo === DIFF DBFNTX (oracle) vs ADSNTX (OpenADS) ===
fc qa_DBFNTX.log qa_ADSNTX.log
popd
