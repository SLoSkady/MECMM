@echo off
rem Removes the mod from the game folder. The game must be closed.
rem Usage: uninstall.bat ["path\to\Mirrors Edge Catalyst"]
set "GAME=E:\SteamLibrary\steamapps\common\Mirrors Edge Catalyst"
if not "%~1"=="" set "GAME=%~1"
tasklist /fi "imagename eq MirrorsEdgeCatalyst.exe" | find /i "MirrorsEdgeCatalyst.exe" >nul && (
    echo Close the game first.
    exit /b 1
)
del /q "%GAME%\dinput8.dll" 2>nul
del /q "%GAME%\mecmm.log" 2>nul
echo Removed. The game is back to normal.
