@echo off
cd /d C:\OpenADS\tests\smoke\harbour
set PATH=C:\harbour\bin\win\msvc64;C:\OpenADS\tests\smoke\harbour;%PATH%
sizecmp.exe
echo DONE_%ERRORLEVEL%
dir sizecmp.log
