@echo off
echo Testing driver restart tool...
echo.
echo Running with verbose mode...
"D:\repos\reshade-display-commander\build\tools\driver_restart.exe" /v
echo.
echo Exit code: %ERRORLEVEL%
pause