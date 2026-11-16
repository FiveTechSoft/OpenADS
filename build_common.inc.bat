@echo off
:: Shared setup for build_server.bat / build_client.bat (not run directly).
:: Sets BUILD_DIR and CLEAN_FLAG in the caller's environment, finds CMake,
:: and configures build\msvc-x86 the first time.

set "SRC_DIR=%~dp0"
set "SRC_DIR=%SRC_DIR:~0,-1%"
set "BUILD_DIR=%SRC_DIR%\build\msvc-x86"
set "CLEAN_FLAG="
for %%A in (%*) do if /I "%%A"=="clean" set "CLEAN_FLAG=--clean-first"

:: CMake. First choice: the cmake.exe that configured build\msvc-x86 (its
:: CMakeCache records it). A different CMake version on PATH breaks the
:: automatic re-configure that runs whenever a CMakeLists.txt changes
:: ("No preprocessor test for Renesas"). Then Visual Studio's bundled
:: copy, then whatever cmake is on PATH.
set "CACHE_CMAKE="
if exist "%BUILD_DIR%\CMakeCache.txt" for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_COMMAND:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHE_CMAKE=%%B"
if defined CACHE_CMAKE set "CACHE_CMAKE=%CACHE_CMAKE:/=\%"
if defined CACHE_CMAKE if exist "%CACHE_CMAKE%" (
    for %%F in ("%CACHE_CMAKE%") do set "PATH=%%~dpF;%PATH%"
    goto :have_cmake
)
for %%D in ("R:\Visual Studio\2022\Community"
            "C:\Program Files\Microsoft Visual Studio\2022\Community"
            "C:\Program Files\Microsoft Visual Studio\2022\Professional"
            "C:\Program Files\Microsoft Visual Studio\2022\Enterprise") do (
    if exist "%%~D\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
        set "PATH=%%~D\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
        goto :have_cmake
    )
)
where cmake >nul 2>&1
if not errorlevel 1 goto :have_cmake
echo CMake not found. Install Visual Studio 2022 with C++ CMake tools, or put cmake on PATH.
exit /b 1

:have_cmake
if exist "%BUILD_DIR%\CMakeCache.txt" exit /b 0
echo === First run: configuring %BUILD_DIR% ===
pushd "%SRC_DIR%"
cmake --preset msvc-x86 -DOPENADS_WARNINGS_AS_ERRORS=OFF
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
