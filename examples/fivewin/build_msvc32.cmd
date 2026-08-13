@echo off
rem build_msvc32.cmd — build xbrowse_ads.prg with FiveWin + Harbour +
rem MSVC 32-bit, linking Harbour's rddads contrib + OpenADS' openace32.lib
rem (so the produced exe drives Advantage tables through OpenADS' DLL).
rem
rem Adapted from FWH's samples\build_new.bat :HM32 path, with one extra
rem link entry: openace32.lib (import lib for OpenADS' openace32.dll).
rem
rem Usage: build_msvc32.cmd [path-to-openads-openace32-dir]
rem   default: ..\..\build\msvc-x86\src\Release
rem Run xbrowse_ads.exe with OpenADS' openace32.dll on PATH (the script
rem copies it next to the exe). xbrowse_ads.exe /auto = self-closing.

setlocal
if "%FWDIR%"=="" set "FWDIR=c:\fwteam"
if "%HBDIR%"=="" set "HBDIR=c:\harbour"
set "HDIRL=%HBDIR%\lib\win\msvc"
set "OPENADS_DLL=%~1"
if "%OPENADS_DLL%"=="" set "OPENADS_DLL=%~dp0..\..\build\dist\openads-1.8.74-windows-x86"
set "PRG=%~2"
if "%PRG%"=="" set "PRG=xbrowse_ads"

rem MSVC 32-bit environment
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
if errorlevel 1 (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
)
where link.exe

echo [fwh] harbour %PRG%.prg ...
"%HBDIR%\bin\win\msvc\harbour" %PRG% /n /i"%FWDIR%\include";"%HBDIR%\include" /w /p || goto :err

echo [fwh] cl %PRG%.c ...
cl -TC -W3 -O2 -c -I"%HBDIR%\include" -D__FLAT__ -I"%FWDIR%\include" %PRG%.c || goto :err

echo [fwh] link ...
rem Lib list mirrors FWH samples\build_new.bat :HM32, plus
rem OpenADS' openace32.lib.
> msvc.tmp echo %PRG%.obj
>> msvc.tmp echo "%FWDIR%\lib\FiveH32.lib" "%FWDIR%\lib\FiveHC32.lib" "%FWDIR%\lib\libmysql_msvc.lib"
>> msvc.tmp echo "%HDIRL%\hbrtl.lib"
>> msvc.tmp echo "%HDIRL%\hbvm.lib"
>> msvc.tmp echo "%HDIRL%\gtgui.lib"
>> msvc.tmp echo "%HDIRL%\hblang.lib"
>> msvc.tmp echo "%HDIRL%\hbmacro.lib"
>> msvc.tmp echo "%HDIRL%\hbrdd.lib"
>> msvc.tmp echo "%HDIRL%\rddntx.lib"
>> msvc.tmp echo "%HDIRL%\rddcdx.lib"
>> msvc.tmp echo "%HDIRL%\rddfpt.lib"
>> msvc.tmp echo "%HDIRL%\hbsix.lib"
>> msvc.tmp echo "%HDIRL%\rddads.lib"
>> msvc.tmp echo "%OPENADS_DLL%\openace32.lib"
>> msvc.tmp echo "%HDIRL%\hbdebug.lib"
>> msvc.tmp echo "%HDIRL%\hbcommon.lib"
>> msvc.tmp echo "%HDIRL%\hbpp.lib"
>> msvc.tmp echo "%HDIRL%\hbcpage.lib"
>> msvc.tmp echo "%HDIRL%\hbwin.lib"
>> msvc.tmp echo "%HDIRL%\hbct.lib"
>> msvc.tmp echo "%HDIRL%\hbziparc.lib"
>> msvc.tmp echo "%HDIRL%\hbmzip.lib"
>> msvc.tmp echo "%HDIRL%\hbzlib.lib"
>> msvc.tmp echo "%HDIRL%\hbpcre.lib"
>> msvc.tmp echo "%HDIRL%\minizip.lib"
>> msvc.tmp echo "%HDIRL%\xhb.lib"
>> msvc.tmp echo "%HDIRL%\hbcplr.lib"
>> msvc.tmp echo "%HDIRL%\png.lib"
>> msvc.tmp echo "%HDIRL%\hbtip.lib"
>> msvc.tmp echo "%HDIRL%\hbzebra.lib"
>> msvc.tmp echo "%HDIRL%\hbcurl.lib"
>> msvc.tmp echo "%HDIRL%\libcurl.lib"
>> msvc.tmp echo kernel32.lib user32.lib gdi32.lib winspool.lib comctl32.lib comdlg32.lib
>> msvc.tmp echo advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib
>> msvc.tmp echo odbccp32.lib iphlpapi.lib mpr.lib version.lib wsock32.lib msimg32.lib
>> msvc.tmp echo oledlg.lib psapi.lib gdiplus.lib winmm.lib ws2_32.lib uxtheme.lib msvcrt.lib
if exist %PRG%.res >> msvc.tmp echo %PRG%.res
del /q %PRG%.exe 2>nul
rem Drive the link through cl (resolves the MSVC toolchain reliably; a
rem bare `link` can pick up a same-named tool from PATH).
cl @msvc.tmp /Fe:%PRG%.exe /nologo /link /subsystem:windows /NODEFAULTLIB:LIBCMT || goto :err
if not exist %PRG%.exe goto :err

echo [fwh] copying OpenADS openace32.dll next to the exe ...
copy /y "%OPENADS_DLL%\openace32.dll" . >nul 2>&1

echo [fwh] done: %PRG%.exe   (smoke run: %PRG%.exe /auto)
endlocal & exit /b 0

:err
echo [fwh] BUILD FAILED (errorlevel %errorlevel%)
endlocal & exit /b 1
