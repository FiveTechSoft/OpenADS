@echo off
rem Builds the OpenADS end-to-end regression PRG for BOTH bitnesses:
rem   b_big_e2e64.exe  (Harbour msvc64 + OpenADS ace64 import lib)
rem   b_big_e2e32.exe  (Harbour msvc   + OpenADS ace32 import lib)
rem
rem Run from anywhere; requires MSVC 2022 + Harbour at C:\harbour and
rem OpenADS built at C:\OpenADS\build\default (x64) and
rem C:\OpenADS\build-x86 (x86).

set HB=C:\harbour
set OADS64=C:\OpenADS\build\default\src\Release
set OADS32=C:\OpenADS\build-x86\src\Release

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0

echo === x64 ===
set PATH=%HB%\bin\win\msvc64;%OADS64%;%PATH%
%HB%\bin\win\msvc64\hbmk2.exe -comp=msvc64 -mt -i"%HB%\contrib\rddads" -lrddads -i"C:/OpenADS/include" "%OADS64%\openace64.lib" -llegacy_stdio_definitions -loldnames -ob_big_e2e64.exe b_big_e2e.prg ../../contrib/oads_hb/oads_hb.c
if errorlevel 1 goto :fail
copy /y "%OADS64%\openace64.dll" .\ace64.dll >nul

echo === x86 ===
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
set PATH=%HB%\bin\win\msvc;%OADS32%;%PATH%
rem 32-bit rddads.lib imports the __stdcall-mangled (@N) ACE names.
rem openace32.dll exports exactly those (src/abi/ace_stdcall_x86.c), and
rem the CMake-generated openace32.lib next to it carries the same symbols.
%HB%\bin\win\msvc\hbmk2.exe -comp=msvc -mt -i"%HB%\contrib\rddads" -i"C:/OpenADS/include" -lrddads "%OADS32%\openace32.lib" -llegacy_stdio_definitions -loldnames -lmsvcrt -lmsvcprt -ob_big_e2e32.exe b_big_e2e.prg ../../contrib/oads_hb/oads_hb.c
if errorlevel 1 goto :fail
copy /y "%OADS32%\openace32.dll" .\openace32.dll >nul

echo OK: b_big_e2e64.exe and b_big_e2e32.exe built.
goto :eof

:fail
echo BUILD FAILED
exit /b 1
