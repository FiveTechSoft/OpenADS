@echo off
:: Build the OpenADS server (openads_serverd.exe, 32-bit, Release).
::
::   build_server.bat          incremental build
::   build_server.bat clean    full rebuild
::
:: Output: build\msvc-x86\tools\serverd\Release\openads_serverd.exe
:: Nothing is copied anywhere: to deploy, stop the running server, copy the
:: exe to C:\OpenAdsR and start it again.

setlocal
call "%~dp0build_common.inc.bat" %* || goto :fail

echo.
echo === Building openads_serverd (x86 Release) ===
cmake --build "%BUILD_DIR%" --config Release --target openads_serverd --parallel %CLEAN_FLAG%
if errorlevel 1 goto :fail

echo.
echo === Server build OK ===
echo   %BUILD_DIR%\tools\serverd\Release\openads_serverd.exe
endlocal & exit /b 0

:fail
echo.
echo === SERVER BUILD FAILED ===
endlocal & exit /b 1
