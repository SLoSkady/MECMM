@echo off
rem Starts a test bot that runs in circles around the first player.
rem Usage: run-bot.bat [host] [port] [name] [radius_m]   (defaults: 127.0.0.1 47777 Bot 5)
title MECMM bot
"%~dp0..\build\Release\mecmm-bot.exe" %*
pause
