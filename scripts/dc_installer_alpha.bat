@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads bleeding-edge (latest_build) addon .addon64 and .addon32 to a central location;
:: creates symbolic links in the script folder to the central copies; runs: rundll32 zzz_display_commander.addon64,CommandLine UiTest
:: Only updates when the release asset is newer (checks via HTTP HEAD: ETag or Last-Modified+Content-Length).
:: Uses curl when available (built-in on Windows 10+) to reduce AV false positives; falls back to PowerShell.
:: Symlinks may require "Run as administrator" or Developer Mode on Windows.

set "BASE=https://github.com/pmnoxx/display-commander/releases/download/latest_build"
set "OUT64=zzz_display_commander.addon64"
set "OUT32=zzz_display_commander.addon32"

:: Central location for addon files (one copy per machine)
set "CENTRAL_DIR=%LOCALAPPDATA%\Programs\Display Commander"
set "CENTRAL64=%CENTRAL_DIR%\%OUT64%"
set "CENTRAL32=%CENTRAL_DIR%\%OUT32%"
set "VER64=%OUT64%.version"
set "VER32=%OUT32%.version"
set "CENTRAL_VER64=%CENTRAL_DIR%\%VER64%"
set "CENTRAL_VER32=%CENTRAL_DIR%\%VER32%"

cd /d "%~dp0"
set "LOCAL_DIR=%~dp0"
if "%LOCAL_DIR:~-1%"=="\" set "LOCAL_DIR=%LOCAL_DIR:~0,-1%"

:: Create central directory
if not exist "%CENTRAL_DIR%" mkdir "%CENTRAL_DIR%"

:: If Windows Defender blocks the file (false positive), add an exclusion:
::   Settings ^> Privacy ^& security ^> Windows Security ^> Virus ^& threat protection ^> Manage settings ^> Exclusions ^> Add folder ^> choose central dir or this script's folder

:: Check if 64-bit addon is up to date in central location; download only if missing or outdated.
set "NEED64=1"
set "REMOTE_VER64="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT64%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%CENTRAL64%') -and (Test-Path '%CENTRAL_VER64%')) { $cur = Get-Content -LiteralPath '%CENTRAL_VER64%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER64%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED64=0"
)
if !errorlevel! equ 2 if exist "%CENTRAL64%" set "NEED64=0"
if !NEED64! equ 1 set /p REMOTE_VER64=<"%VER64%.tmp" 2>nul
del "%VER64%.tmp" 2>nul

if !NEED64! equ 1 (
    echo Downloading %OUT64% to central location ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%CENTRAL64%" "%BASE%/%OUT64%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT64%' -OutFile '%CENTRAL64%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER64 (echo !REMOTE_VER64!> "%CENTRAL_VER64%")
    echo Saved as %CENTRAL64%
) else (
    echo Using existing %OUT64% ^(up to date^)
)

:: Check if 32-bit addon is up to date in central location; download only if missing or outdated.
set "NEED32=1"
set "REMOTE_VER32="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT32%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%CENTRAL32%') -and (Test-Path '%CENTRAL_VER32%')) { $cur = Get-Content -LiteralPath '%CENTRAL_VER32%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER32%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED32=0"
)
if !errorlevel! equ 2 if exist "%CENTRAL32%" set "NEED32=0"
if !NEED32! equ 1 set /p REMOTE_VER32=<"%VER32%.tmp" 2>nul
del "%VER32%.tmp" 2>nul

if !NEED32! equ 1 (
    echo Downloading %OUT32% to central location ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%CENTRAL32%" "%BASE%/%OUT32%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT32%' -OutFile '%CENTRAL32%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER32 (echo !REMOTE_VER32!> "%CENTRAL_VER32%")
    echo Saved as %CENTRAL32%
) else (
    echo Using existing %OUT32% ^(up to date^)
)

:: In script folder: remove existing .addon64/.addon32 (files or links), then create symlink or hard link to central location.
:: Hard link is used when symlink fails (no admin / Developer Mode); same volume required.
if exist "%OUT64%" del "%OUT64%" 2>nul
if exist "%OUT64%" rmdir "%OUT64%" 2>nul
mklink "%OUT64%" "%CENTRAL64%" >nul 2>&1
if errorlevel 1 (
    mklink /H "%OUT64%" "%CENTRAL64%" >nul 2>&1
    if errorlevel 1 (
        echo Failed to link %OUT64% to central copy. Run as Administrator or enable Developer Mode for symlink.
        exit /b 1
    )
    echo Linked ^(hard^): %LOCAL_DIR%\%OUT64% -^> %CENTRAL64%
) else (
    echo Linked ^(symlink^): %LOCAL_DIR%\%OUT64% -^> %CENTRAL64%
)
if exist "%OUT32%" del "%OUT32%" 2>nul
if exist "%OUT32%" rmdir "%OUT32%" 2>nul
mklink "%OUT32%" "%CENTRAL32%" >nul 2>&1
if errorlevel 1 (
    mklink /H "%OUT32%" "%CENTRAL32%" >nul 2>&1
    if errorlevel 1 (
        echo Failed to link %OUT32% to central copy. Run as Administrator or enable Developer Mode for symlink.
        exit /b 1
    )
    echo Linked ^(hard^): %LOCAL_DIR%\%OUT32% -^> %CENTRAL32%
) else (
    echo Linked ^(symlink^): %LOCAL_DIR%\%OUT32% -^> %CENTRAL32%
)

echo Running: rundll32 %OUT64%,CommandLine UiTest
rundll32 "%OUT64%",CommandLine UiTest
