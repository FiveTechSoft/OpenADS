@echo off
setlocal
set HB_INSTALL=c:\harbour
set OPENADS_LIB=C:\OpenADS\build\default\src\Release
if not exist "%OPENADS_LIB%\openace64.dll" set OPENADS_LIB=C:\OpenADS\build\release-x64\src\Release
set PATH=%HB_INSTALL%\bin\win\msvc64;%OPENADS_LIB%;%PATH%

call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
if errorlevel 1 (
  call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64
)

cd /d %~dp0
echo [hbmk2] OPENADS_LIB=%OPENADS_LIB%
hbmk2 -comp=msvc64 -gtcgi -i%HB_INSTALL%\contrib\rddads openads_demo_remote.hbp > build_remote.log 2>&1
set RC=%ERRORLEVEL%
type build_remote.log
if %RC% neq 0 exit /b %RC%

copy /y "%OPENADS_LIB%\openace64.dll" ace64.dll >nul 2>&1
if errorlevel 1 copy /y "%OPENADS_LIB%\ace64.dll" ace64.dll >nul
echo [ok] openads_demo_remote.exe
endlocal
exit /b 0