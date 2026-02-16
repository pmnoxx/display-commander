@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads bleeding-edge (latest_build) addon .addon64 and .addon32; runs: rundll32 zzz_display_commander.addon64,CommandLine UiTest
:: Uses curl when available (built-in on Windows 10+) to reduce AV false positives; falls back to PowerShell.

set "BASE=https://github.com/pmnoxx/display-commander/releases/download/latest_build"
set "OUT64=zzz_display_commander.addon64"
set "OUT32=zzz_display_commander.addon32"

cd /d "%~dp0"

:: If Windows Defender blocks the file (false positive), add an exclusion:
::   Settings ^> Privacy ^& security ^> Windows Security ^> Virus ^& threat protection ^> Manage settings ^> Exclusions ^> Add folder ^> choose this script's folder

:: Download 64-bit addon if missing
if not exist "%OUT64%" (
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
    echo Saved as %OUT64%
) else (
    echo Using existing %OUT64%
)

:: Download 32-bit addon if missing
if not exist "%OUT32%" (
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
    echo Saved as %OUT32%
) else (
    echo Using existing %OUT32%
)

echo Running: rundll32 %OUT64%,CommandLine UiTest
rundll32 "%OUT64%",CommandLine UiTest
