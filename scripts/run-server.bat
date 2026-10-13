@echo off
rem Starts the MECMM relay server in this window. Usage: run-server.bat [port]
title MECMM server
"%~dp0..\build\Release\mecmm-server.exe" %*
pause
