@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\OpenADS\tests\smoke\harbour
set PATH=C:\harbour\bin\win\msvc64;%PATH%
hbmk2 -comp=msvc64 idx03dump.prg -L. -lrddads -lace64 -llegacy_stdio_definitions -loldnames
idx03dump.exe
