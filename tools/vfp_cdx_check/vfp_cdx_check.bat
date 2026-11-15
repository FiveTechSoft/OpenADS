@echo off
rem vfp_cdx_check -- verify the CDX indexes in a folder with Visual FoxPro 9.
rem
rem   vfp_cdx_check.bat <data-folder> [table-mask] [result-file]
rem
rem   data-folder  folder holding the .dbf / .cdx files
rem   table-mask   default *.dbf   (e.g. TRANS.dbf for one table)
rem   result-file  default <data-folder>\vfp_cdx_check.txt
rem
rem Tables are opened read-only (NOUPDATE), but running against a COPY of
rem the data is still recommended: no peer locks, no surprises.
rem Set VFP9_EXE to override the VFP 9 location.
rem Exit code: 0 = every tag ok, 1 = BAD / ERR / OPEN FAILED found,
rem            2 = usage / VFP not found / no result written.
setlocal
if "%~1"=="" (
    echo usage: %~nx0 ^<data-folder^> [table-mask] [result-file]
    exit /b 2
)
if not exist "%~1\" (
    echo data folder not found: "%~1"
    exit /b 2
)
if not defined VFP9_EXE set "VFP9_EXE=%ProgramFiles(x86)%\Microsoft Visual FoxPro 9\vfp9.exe"
if not exist "%VFP9_EXE%" (
    rem Quoted: "Program Files (x86)" would close this ( ) block early.
    echo vfp9.exe not found: "%VFP9_EXE%" -- set VFP9_EXE
    exit /b 2
)
set "VFPCHK_DIR=%~f1"
set "VFPCHK_MASK=%~2"
if "%~3"=="" (set "VFPCHK_OUT=%~f1\vfp_cdx_check.txt") else (set "VFPCHK_OUT=%~f3")
if exist "%VFPCHK_OUT%" del "%VFPCHK_OUT%"

echo Checking %VFPCHK_DIR% with VFP 9 ...
rem start /wait: vfp9.exe is a GUI program; wait for check.prg to QUIT.
start "" /wait "%VFP9_EXE%" -t "%~dp0check.prg"

if not exist "%VFPCHK_OUT%" (
    echo no result written -- VFP may have stopped on a dialog
    exit /b 2
)
findstr /b /c:"BAD" /c:"ERR" /c:"   !!" "%VFPCHK_OUT%"
findstr /c:"OPEN FAILED" "%VFPCHK_OUT%"
findstr /b /c:"SUMMARY" "%VFPCHK_OUT%"
echo Full report: %VFPCHK_OUT%
findstr /b /c:"BAD" /c:"ERR" "%VFPCHK_OUT%" >nul && exit /b 1
findstr /c:"OPEN FAILED" "%VFPCHK_OUT%" >nul && exit /b 1
exit /b 0
