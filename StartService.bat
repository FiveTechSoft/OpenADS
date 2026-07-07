@echo off
rem Starts the already-registered openads_serverd Windows Service.
rem --service is an SCM-only flag (main.cpp checks it via StartServiceCtrlDispatcherA
rem and fails outside the SCM) -- it must never be passed on a manually-run command
rem line. To change --port/--data, reinstall the service instead:
rem   openads_serverd.exe --uninstall-service
rem   openads_serverd.exe --install-service --port 16262 --data "F:\OpenADS\testdata\pmsys;c:\pmsys\data"
sc start openads_serverd
