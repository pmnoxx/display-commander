@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads bleeding-edge (latest_build) addon as dc_launcher.dc (not .dll / .addon64) and runs: rundll32 dc_launcher.dc,CommandLine UiTest
:: Uses curl when available (built-in on Windows 10+) to reduce AV false positives; falls back to PowerShell.

set "URL=https://github.com/pmnoxx/display-commander/releases/download/latest_build/zzz_display_commander.addon64"
set "OUT=dc_launcher.dc"

cd /d "%~dp0"

:: If Windows Defender blocks the file (false positive), add an exclusion:
::   Settings ^> Privacy ^& security ^> Windows Security ^> Virus ^& threat protection ^> Manage settings ^> Exclusions ^> Add folder ^> choose this script's folder

if not exist "%OUT%" (
    echo Downloading %URL% ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%OUT%" "%URL%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%URL%' -OutFile '%OUT%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo Download failed.
        exit /b 1
    )
    echo Saved as %OUT%
) else (
    echo Using existing %OUT%
)

echo Running: rundll32 %OUT%,CommandLine UiTest
rundll32 "%OUT%",CommandLine UiTest
