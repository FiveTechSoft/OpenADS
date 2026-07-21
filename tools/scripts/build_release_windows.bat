@echo off
REM Build a Windows release ZIP containing BOTH ace64.dll (x64) and
REM ace32.dll (x86) so the same archive serves both bitnesses. X#,
REM Harbour x86, and legacy Clipper apps all dynamically pick the
REM matching DLL at runtime — shipping only one is a recurring
REM friction point reported by users.
REM
REM Usage:
REM   tools\scripts\build_release_windows.bat <output-dir>
REM
REM Requirements:
REM   - VS 2022 with both x64 and x86 build tools
REM   - cmake on PATH
REM
REM Notes:
REM   - mbedtls is statically linked (no runtime OpenSSL DLL deps).
REM   - The bundled ZIP exposes openace64.dll + ace64.dll (and the
REM     32-bit pair), import libs for both names, server, and bench.

setlocal ENABLEDELAYEDEXPANSION
if "%~1"=="" (
    echo usage: %~nx0 ^<output-dir^>
    exit /b 1
)
set OUT=%~f1
if not exist "%OUT%" mkdir "%OUT%"

set VSDEV="%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist %VSDEV% (
    echo VsDevCmd.bat not found; install VS 2022 Community.
    exit /b 1
)

set ROOT=%~dp0..\..
set ROOT=%ROOT:\=/%

REM ---- x64 build -----------------------------------------------------
echo [build_release] x64 configure + build...
call %VSDEV% -arch=amd64 -no_logo
rem Studio (localhost:6263) requires OPENADS_WITH_HTTP=ON — do not omit.
cmake -S "%ROOT%" -B "%ROOT%/build/release-x64" -G "Visual Studio 17 2022" -A x64 -DOPENADS_WITH_TLS=ON -DOPENADS_WITH_HTTP=ON
if errorlevel 1 exit /b 1
cmake --build "%ROOT%/build/release-x64" --config Release -j
if errorlevel 1 exit /b 1

copy /Y "%ROOT%\build\release-x64\src\Release\openace64.dll" "%OUT%\"
copy /Y "%ROOT%\build\release-x64\src\Release\openace64.lib" "%OUT%\"
copy /Y "%ROOT%\build\release-x64\src\Release\openace64.dll" "%OUT%\ace64.dll"
copy /Y "%ROOT%\build\release-x64\src\Release\openace64.lib" "%OUT%\ace64.lib"
copy /Y "%ROOT%\build\release-x64\tools\serverd\Release\openads_serverd.exe" "%OUT%\openads_serverd_x64.exe"
copy /Y "%ROOT%\build\release-x64\tools\bench\Release\openads_bench.exe"     "%OUT%\openads_bench_x64.exe"

REM ---- x86 build -----------------------------------------------------
echo [build_release] x86 configure + build...
call %VSDEV% -arch=x86 -no_logo
cmake -S "%ROOT%" -B "%ROOT%/build/release-x86" -G "Visual Studio 17 2022" -A Win32 -DOPENADS_WITH_TLS=ON -DOPENADS_WITH_HTTP=ON -DOPENADS_ACE_DEF="%ROOT%/src/openads_ace_x86.def"
if errorlevel 1 exit /b 1
cmake --build "%ROOT%/build/release-x86" --config Release -j
if errorlevel 1 exit /b 1

copy /Y "%ROOT%\build\release-x86\src\Release\openace32.dll" "%OUT%\"
copy /Y "%ROOT%\build\release-x86\src\Release\openace32.lib" "%OUT%\"
copy /Y "%ROOT%\build\release-x86\src\Release\openace32.dll" "%OUT%\ace32.dll"
copy /Y "%ROOT%\build\release-x86\src\Release\openace32.lib" "%OUT%\ace32.lib"
copy /Y "%ROOT%\build\release-x86\tools\serverd\Release\openads_serverd.exe" "%OUT%\openads_serverd_x86.exe"
copy /Y "%ROOT%\build\release-x86\tools\bench\Release\openads_bench.exe"     "%OUT%\openads_bench_x86.exe"

REM ---- license + readme ---------------------------------------------
copy /Y "%ROOT%\LICENSE" "%OUT%\"
copy /Y "%ROOT%\README.md" "%OUT%\"

echo [build_release] done. Files in %OUT%
dir /b "%OUT%"

endlocal
