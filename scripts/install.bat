@echo off
rem Copies the built mod into the game folder. The game must be closed.
rem Usage: install.bat ["path\to\Mirrors Edge Catalyst"]
set "GAME=E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst"
if not "%~1"=="" set "GAME=%~1"
tasklist /fi "imagename eq MirrorsEdgeCatalyst.exe" | find /i "MirrorsEdgeCatalyst.exe" >nul && (
    echo Close the game first.
    exit /b 1
)
if not exist "%GAME%\MirrorsEdgeCatalyst.exe" (
    echo Game not found in "%GAME%". Pass the game folder as an argument.
    exit /b 1
)
copy /y "%~dp0..\build\Release\dinput8.dll" "%GAME%\dinput8.dll" >nul || exit /b 1
echo Installed: %GAME%\dinput8.dll
