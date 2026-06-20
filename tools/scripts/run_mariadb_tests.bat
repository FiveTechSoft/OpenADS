@echo off
REM Run MariaDB ABI e2e tests from build\maria-msvc (or pass BUILD_DIR).
setlocal
set "ROOT=%~dp0..\.."
set "BUILD=%ROOT%\build\maria-msvc"
if not "%~1"=="" set "BUILD=%~1"

if not exist "%BUILD%\tests\openads_unit_tests.exe" (
    echo ERROR: %BUILD%\tests\openads_unit_tests.exe not found. Build first.
    exit /b 1
)

if not defined OPENADS_TOOLCHAIN_ROOT (
    echo ERROR: set OPENADS_TOOLCHAIN_ROOT to the directory that holds the
    echo        mariadb client and winlibs (for libmariadb PATH).
    exit /b 1
)

set "PATH=%OPENADS_TOOLCHAIN_ROOT%\mariadb\bin;%PATH%"
if exist "%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin" (
    set "PATH=%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin;%PATH%"
)

if not defined OPENADS_TEST_MARIADB_URI (
    set "OPENADS_TEST_MARIADB_URI=mariadb://root@127.0.0.1:3306/test"
)

cd /d "%BUILD%"
tests\openads_unit_tests.exe --test-case=*mariadb* %*
exit /b %ERRORLEVEL%