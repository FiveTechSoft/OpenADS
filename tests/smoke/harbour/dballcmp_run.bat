@echo off
rem dballcmp_run.bat -- build dballcmp.prg (32-bit Harbour/MinGW + rddads
rem linked to openace32.dll) and run all three passes: DBFCDX, ADSCDX
rem local, ADSCDX through a private openads_serverd.
rem
rem Defaults match the xoffice3 toolchain; override with env vars:
rem   HB_BIN       Harbour bin dir with hbmk2.exe  (s:\harbour\HarbourQT4\bin\win\mingw)
rem   MINGW_BIN    MinGW bin dir with gcc.exe      (S:\mingw492\bin)
rem   OPENADS_BLD  OpenADS x86 build dir           (<repo>\build\msvc-x86)
rem   DBALLCMP_DIR work folder (data + exe)        (%TEMP%\openads_dballcmp)
rem   DBALLCMP_PORT server port                    (6299)
rem
rem Result: %DBALLCMP_DIR%\dballcmp_verdict.txt; exit code 0 = all PASS.
setlocal
set HERE=%~dp0
if not defined HB_BIN set HB_BIN=s:\harbour\HarbourQT4\bin\win\mingw
if not defined MINGW_BIN set MINGW_BIN=S:\mingw492\bin
if not defined OPENADS_BLD set OPENADS_BLD=%HERE%..\..\..\build\msvc-x86
if not defined DBALLCMP_DIR set DBALLCMP_DIR=%TEMP%\openads_dballcmp
if not defined DBALLCMP_PORT set DBALLCMP_PORT=6299
set IMPLIB=%HERE%..\..\..\dist\import-libs\x86\mingw\libopenace32.dll.a

if not exist "%DBALLCMP_DIR%\lib" mkdir "%DBALLCMP_DIR%\lib"
copy /y "%HERE%dballcmp.prg" "%DBALLCMP_DIR%\" >nul
copy /y "%HERE%ads.ch" "%DBALLCMP_DIR%\" >nul
rem hbmk2 links -Bstatic; the import lib must be found as lib<name>.a
copy /y "%IMPLIB%" "%DBALLCMP_DIR%\lib\libopenace32.a" >nul || goto :err
copy /y "%OPENADS_BLD%\src\Release\openace32.dll" "%DBALLCMP_DIR%\" >nul || goto :err
rem Private server copy under its own name: the taskkill below must never
rem touch a production openads_serverd.exe.
copy /y "%OPENADS_BLD%\tools\serverd\Release\openads_serverd.exe" "%DBALLCMP_DIR%\dballcmp_serverd.exe" >nul || goto :err

pushd "%DBALLCMP_DIR%"
set PATH=%HB_BIN%;%MINGW_BIN%;%WINDIR%;%WINDIR%\SYSTEM32
set HB_INSTALL_PREFIX=
echo === BUILD ===
if exist dballcmp.exe del dballcmp.exe
hbmk2 -comp=mingw -gtcgi -q0 -lrddads -L"%DBALLCMP_DIR%\lib" -lopenace32 dballcmp.prg
if not exist dballcmp.exe ( popd & goto :err )

echo === SERVER ===
taskkill /f /im dballcmp_serverd.exe >nul 2>&1
start "dballcmp_serverd" /b "%DBALLCMP_DIR%\dballcmp_serverd.exe" --port %DBALLCMP_PORT% --data "%DBALLCMP_DIR:\=/%" > dballcmp_server.log 2>&1
ping -n 3 127.0.0.1 >nul

echo === RUN ===
set OPENADS_TEST_URI=tcp://127.0.0.1:%DBALLCMP_PORT%/%DBALLCMP_DIR:\=/%
"%DBALLCMP_DIR%\dballcmp.exe"
set RC=%ERRORLEVEL%
taskkill /f /im dballcmp_serverd.exe >nul 2>&1
echo.
echo transcripts + verdict: %DBALLCMP_DIR%
popd
exit /b %RC%

:err
echo BUILD/SETUP FAILED
exit /b 2
