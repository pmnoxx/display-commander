@echo off
REM Copy Display Commander 64-bit addon to the proxy DLL names that DC supports (d3d9, d3d11, d3d12, ddraw, dinput8, hid, bcrypt, opengl32, dxgi, version, winmm, dbghelp, vulkan-1).
REM Usage: run from the folder that contains zzz_display_commander.addon64, or pass that folder as the first argument.
setlocal enabledelayedexpansion
set "TARGET=%~1"
if not defined TARGET set "TARGET=%CD%"
set "ADDON=%TARGET%\zzz_display_commander.addon64"
if not exist "%ADDON%" (
    echo Addon not found: %ADDON%
    echo Place zzz_display_commander.addon64 in the target folder or pass the folder as the first argument.
    exit /b 1
)
set "COUNT=0"
for %%D in (d3d9.dll d3d11.dll d3d12.dll ddraw.dll dinput8.dll hid.dll bcrypt.dll opengl32.dll dxgi.dll version.dll winmm.dll dbghelp.dll vulkan-1.dll) do (
    copy /Y "%ADDON%" "%TARGET%\%%D" >nul 2>&1 && set /a COUNT+=1
)
echo Copied addon to !COUNT! proxy DLLs in %TARGET%
endlocal
