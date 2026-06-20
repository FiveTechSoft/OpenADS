@echo off
REM MariaDB backend build with MSVC cl (no winlibs GCC runtime at run time).
if not defined OPENADS_TOOLCHAIN_ROOT (
    echo ERROR: set OPENADS_TOOLCHAIN_ROOT to the directory that holds the
    echo        msvc, winlibs and mariadb client dependency folders.
    exit /b 1
)
if not defined OPENADS_LIBMARIADB_INCLUDE (
    set "OPENADS_LIBMARIADB_INCLUDE=%OPENADS_TOOLCHAIN_ROOT%\mariadb\include"
)
if not defined OPENADS_LIBMARIADB_LIBRARY (
    set "OPENADS_LIBMARIADB_LIBRARY=%OPENADS_TOOLCHAIN_ROOT%\mariadb\lib\libmariadb64.lib"
)
call "%OPENADS_TOOLCHAIN_ROOT%\msvc\setup_x64.bat"
if exist "%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin" (
    set "PATH=%OPENADS_TOOLCHAIN_ROOT%\winlibs-x86_64\bin;%PATH%"
)
cd /d "%~dp0..\.."
cmake -S . -B build\maria-msvc -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl ^
    -DOPENADS_WITH_MARIADB=ON -DOPENADS_WITH_HTTP=OFF ^
    -DOPENADS_WARNINGS_AS_ERRORS=OFF ^
    -DOPENADS_LIBMARIADB_INCLUDE=%OPENADS_LIBMARIADB_INCLUDE% ^
    -DOPENADS_LIBMARIADB_LIBRARY=%OPENADS_LIBMARIADB_LIBRARY%
if errorlevel 1 exit /b 1
cmake --build build\maria-msvc
exit /b %ERRORLEVEL%