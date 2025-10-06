@echo off
echo Driver Restart Tool - Administrator Mode
echo ========================================
echo.
echo This will restart your graphics driver.
echo You will be prompted for administrator privileges.
echo.
pause

echo.
echo Running driver restart tool as administrator...
echo.

powershell -Command "Start-Process 'D:\repos\reshade-display-commander\build\tools\driver_restart.exe' -ArgumentList '/v' -Verb RunAs -Wait"

echo.
echo Driver restart completed.
echo.
pause
