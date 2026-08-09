@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\OpenADS\tests\smoke\harbour
dumpbin /dependents sizecmp.exe | findstr /i ".dll"
echo ---- smoke.exe:
dumpbin /dependents smoke.exe | findstr /i ".dll"
