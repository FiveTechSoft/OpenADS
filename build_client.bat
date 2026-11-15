@echo off
:: Build the OpenADS client DLL (openace32.dll, 32-bit, Release).
::
::   build_client.bat          incremental build
::   build_client.bat clean    full rebuild
::
:: Output: build\msvc-x86\src\Release\openace32.dll  (+ openace32.lib)
:: Nothing is copied anywhere: to deploy, close the programs using the DLL
:: and copy openace32.dll next to them (or to the folder on their PATH).

setlocal
call "%~dp0build_common.inc.bat" %* || goto :fail

echo.
echo === Building openace32.dll (x86 Release) ===
cmake --build "%BUILD_DIR%" --config Release --target openads_ace --parallel %CLEAN_FLAG%
if errorlevel 1 goto :fail

echo.
echo === Client build OK ===
echo   %BUILD_DIR%\src\Release\openace32.dll
echo   %BUILD_DIR%\src\Release\openace32.lib
endlocal & exit /b 0

:fail
echo.
echo === CLIENT BUILD FAILED ===
endlocal & exit /b 1
