@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads bleeding-edge (latest_build) addon .addon64 and .addon32 to this script's folder.
:: Copies addon as dc_installer64.dll in the same folder and can run: rundll32 dc_installer64.dll,CommandLine SetupDC <script_folder>
:: Only updates when the release asset is newer (checks via HTTP HEAD: ETag or Last-Modified+Content-Length).
:: Uses curl when available (built-in on Windows 10+) to reduce AV false positives; falls back to PowerShell.

set "BASE=https://github.com/pmnoxx/display-commander/releases/download/latest_build"
set "OUT64=zzz_display_commander.addon64"
set "OUT32=zzz_display_commander.addon32"

cd /d "%~dp0"
set "LOCAL_DIR=%~dp0"
if "%LOCAL_DIR:~-1%"=="\" set "LOCAL_DIR=%LOCAL_DIR:~0,-1%"

:: Local paths (script folder)
set "LOCAL64=%LOCAL_DIR%\%OUT64%"
set "LOCAL32=%LOCAL_DIR%\%OUT32%"
set "VER64="
set "VER32="
set "LOCAL_VER64=%LOCAL_DIR%\%VER64%"
set "LOCAL_VER32=%LOCAL_DIR%\%VER32%"
set "INSTALLER64=%LOCAL_DIR%\dc_installer64.dll"

:: If Windows Defender blocks the file (false positive), add an exclusion:
::   Settings ^> Privacy ^& security ^> Windows Security ^> Virus ^& threat protection ^> Manage settings ^> Exclusions ^> Add folder ^> this script's folder

:: Check if 64-bit addon is up to date locally; download only if missing or outdated.
set "NEED64=1"
set "REMOTE_VER64="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT64%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%LOCAL64%') -and (Test-Path '%LOCAL_VER64%')) { $cur = Get-Content -LiteralPath '%LOCAL_VER64%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER64%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED64=0"
)
if !errorlevel! equ 2 if exist "%LOCAL64%" set "NEED64=0"
if !NEED64! equ 1 set /p REMOTE_VER64=<"%VER64%.tmp" 2>nul
del "%VER64%.tmp" 2>nul

if !NEED64! equ 1 (
    echo Downloading %OUT64% to script folder ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%LOCAL64%" "%BASE%/%OUT64%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT64%' -OutFile '%LOCAL64%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER64 (echo !REMOTE_VER64!> "%LOCAL_VER64%")
    echo Saved as %LOCAL64%
) else (
    echo Using existing %OUT64% ^(up to date^)
)

:: Copy local 64-bit addon as dc_installer64.dll for launcher
:: copy /Y "%LOCAL64%" "%INSTALLER64%" >nul
:: if errorlevel 1 (
::     echo Failed to copy %OUT64% to dc_installer64.dll
::     exit /b 1
:: )

:: Check if 32-bit addon is up to date locally; download only if missing or outdated.
set "NEED32=1"
set "REMOTE_VER32="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT32%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%LOCAL32%') -and (Test-Path '%LOCAL_VER32%')) { $cur = Get-Content -LiteralPath '%LOCAL_VER32%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER32%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED32=0"
)
if !errorlevel! equ 2 if exist "%LOCAL32%" set "NEED32=0"
if !NEED32! equ 1 set /p REMOTE_VER32=<"%VER32%.tmp" 2>nul
del "%VER32%.tmp" 2>nul

if !NEED32! equ 1 (
    echo Downloading %OUT32% to script folder ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%LOCAL32%" "%BASE%/%OUT32%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT32%' -OutFile '%LOCAL32%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER32 (echo !REMOTE_VER32!> "%LOCAL_VER32%")
    echo Saved as %LOCAL32%
) else (
    echo Using existing %OUT32% ^(up to date^)
)


:: echo Running: rundll32 dc_installer64.dll,CommandLine SetupDC "%LOCAL_DIR%"
:: rundll32 "%INSTALLER64%",CommandLine SetupDC "%LOCAL_DIR%"
