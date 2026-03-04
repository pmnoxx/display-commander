@echo off
REM Run copy_dc64_to_wine_proxies.py for STEINS;GATE game folder.
REM Place dc64.dll in the game folder first; then run the game with .DLL_DETECTOR to see which DLLs get loaded.
set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\STEINS;GATE"
set "SCRIPT_DIR=%~dp0"
python "%SCRIPT_DIR%copy_dc64_to_wine_proxies.py" "%GAME_DIR%"
pause
