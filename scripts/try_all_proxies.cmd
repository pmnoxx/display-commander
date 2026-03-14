@echo off
REM Run try_all_proxies.py and wait so output is visible.
cd /d "%~dp0"
if not exist "..\build\zzz_display_commander.addon64" (
    echo zzz_display_commander.addon64 not found in ..\build\
    echo Build the project first or place the addon in build\
    pause
    exit /b 1
)
python try_all_proxies.py
pause
