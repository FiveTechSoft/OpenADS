@echo off
rem ttest.cmd -- selective unit-test runner for OpenADS.
rem
rem The full suite is ~1400 doctest cases / 12+ minutes. Run only what
rem you changed; run everything only when you ask for it.
rem
rem Usage:
rem   tests\ttest.cmd Nav*                  only test cases matching Nav*
rem   tests\ttest.cmd "*CDX*" "*seek*"      several patterns (OR)
rem   tests\ttest.cmd all                   full default tier (excludes [slow])
rem   tests\ttest.cmd slow                  only the [slow] tier
rem   tests\ttest.cmd list [patron]         list test case names (default: all)
rem
rem Patterns are doctest wildcards (-tc). Case-sensitive; quote patterns
rem that contain spaces or start with '*'.

setlocal
set EXE=%~dp0..\build\default\tests\Release\openads_unit_tests.exe
if not exist "%EXE%" (
    echo [ttest] not built: %EXE%
    echo [ttest] cmake --build build\default --config Release --target openads_unit_tests
    exit /b 1
)

if "%~1"=="" goto :usage
if /i "%~1"=="all"  goto :all
if /i "%~1"=="slow" goto :slow
if /i "%~1"=="list" goto :list

rem Collect up to 8 patterns into one comma-separated -tc filter.
set FILT=
:collect
if "%~1"=="" goto :runfilt
if defined FILT (set FILT=%FILT%,) 
set FILT=%FILT%%~1
shift
goto :collect

:runfilt
echo [ttest] -tc="%FILT%"
"%EXE%" -tc="%FILT%"
exit /b %ERRORLEVEL%

:all
echo [ttest] full default tier (excluding [slow])
"%EXE%" -e=*[slow]*
exit /b %ERRORLEVEL%

:slow
echo [ttest] slow tier only
"%EXE%" -tc=*[slow]*
exit /b %ERRORLEVEL%

:list
if "%~2"=="" (
    "%EXE%" --list-test-cases
) else (
    "%EXE%" --list-test-cases -tc="%~2"
)
exit /b %ERRORLEVEL%

:usage
echo Usage: tests\ttest.cmd {pattern [pattern...] ^| all ^| slow ^| list [pattern]}
exit /b 2
