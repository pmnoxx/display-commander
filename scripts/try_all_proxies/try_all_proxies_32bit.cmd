@echo off
REM Run try_all_proxies.py and wait so output is visible.
cd /d "%~dp0"
if not exist "zzz_display_commander.addon32" (
    echo zzz_display_commander.addon32 not found
    pause
    exit /b 1
)
python try_all_proxies.py --32
pause
