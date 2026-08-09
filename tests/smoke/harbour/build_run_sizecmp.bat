@echo off
rem Build + run the OpenADS-vs-Harbour header/body comparison smoke.
rem Follows the repo convention: the .hbp is generated, not tracked.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\OpenADS\tests\smoke\harbour
copy /y C:\OpenADS\build\default\src\Release\openace64.dll ace64.dll >nul
copy /y C:\OpenADS\build\default\src\Release\openace64.dll openace64.dll >nul
copy /y C:\OpenADS\build\default\src\Release\openace64.lib ace64.lib >nul
> sizecmp.hbp echo sizecmp.prg
>>sizecmp.hbp echo -lrddads
>>sizecmp.hbp echo -L.
>>sizecmp.hbp echo -lace64
>>sizecmp.hbp echo -llegacy_stdio_definitions
>>sizecmp.hbp echo -loldnames
set PATH=C:\harbour\bin\win\msvc64;%PATH%
hbmk2 -comp=msvc64 sizecmp.hbp
if errorlevel 1 exit /b 1
sizecmp.exe
exit /b %ERRORLEVEL%
