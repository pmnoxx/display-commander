@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads bleeding-edge (latest_build) addon .addon64 and .addon32; runs: rundll32 zzz_display_commander.addon64,CommandLine UiTest
:: Only updates when the release asset is newer (checks via HTTP HEAD: ETag or Last-Modified+Content-Length).
:: Uses curl when available (built-in on Windows 10+) to reduce AV false positives; falls back to PowerShell.

set "BASE=https://github.com/pmnoxx/display-commander/releases/download/latest_build"
set "OUT64=zzz_display_commander.addon64"
set "OUT32=zzz_display_commander.addon32"

cd /d "%~dp0"

:: If Windows Defender blocks the file (false positive), add an exclusion:
::   Settings ^> Privacy ^& security ^> Windows Security ^> Virus ^& threat protection ^> Manage settings ^> Exclusions ^> Add folder ^> choose this script's folder

:: Check if 64-bit addon is up to date; download only if missing or outdated.
set "VER64=%OUT64%.version"
set "NEED64=1"
set "REMOTE_VER64="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT64%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%OUT64%') -and (Test-Path '%VER64%')) { $cur = Get-Content -LiteralPath '%VER64%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER64%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED64=0"
)
if !errorlevel! equ 2 if exist "%OUT64%" set "NEED64=0"
if !NEED64! equ 1 set /p REMOTE_VER64=<"%VER64%.tmp" 2>nul
del "%VER64%.tmp" 2>nul

if !NEED64! equ 1 (
    echo Downloading %OUT64% ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%OUT64%" "%BASE%/%OUT64%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT64%' -OutFile '%OUT64%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER64 (echo !REMOTE_VER64!> "%VER64%")
    echo Saved as %OUT64%
) else (
    echo Using existing %OUT64% ^(up to date^)
)

:: Check if 32-bit addon is up to date; download only if missing or outdated.
set "VER32=%OUT32%.version"
set "NEED32=1"
set "REMOTE_VER32="
powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $r = Invoke-WebRequest -Uri '%BASE%/%OUT32%' -Method Head -UseBasicParsing -MaximumRedirection 5; $et = $r.Headers['ETag']; if ($et) { $ver = $et } else { $ver = $r.Headers['Last-Modified'] + '|' + $r.Headers['Content-Length'] }; Write-Output $ver; if ((Test-Path '%OUT32%') -and (Test-Path '%VER32%')) { $cur = Get-Content -LiteralPath '%VER32%' -Raw; if ($cur.Trim() -eq $ver) { exit 0 } }; exit 1 } catch { exit 2 }" > "%VER32%.tmp" 2>nul
if !errorlevel! equ 0 (
    set "NEED32=0"
)
if !errorlevel! equ 2 if exist "%OUT32%" set "NEED32=0"
if !NEED32! equ 1 set /p REMOTE_VER32=<"%VER32%.tmp" 2>nul
del "%VER32%.tmp" 2>nul

if !NEED32! equ 1 (
    echo Downloading %OUT32% ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%OUT32%" "%BASE%/%OUT32%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%BASE%/%OUT32%' -OutFile '%OUT32%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    if defined REMOTE_VER32 (echo !REMOTE_VER32!> "%VER32%")
    echo Saved as %OUT32%
) else (
    echo Using existing %OUT32% ^(up to date^)
)

echo Running: rundll32 %OUT64%,CommandLine UiTest
rundll32 "%OUT64%",CommandLine UiTest
