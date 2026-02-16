@echo off
SetLocal EnableDelayedExpansion

:: Display Commander launcher for standalone UI test
:: Downloads nightly addon as dc_launcher.dc (not .dll / .addon64) and runs: rundll32 dc_launcher.dc,CommandLine UiTest

set "URL=https://github.com/pmnoxx/display-commander/releases/download/nightly/zzz_display_commander.addon64"
set "OUT=dc_launcher.dc"

cd /d "%~dp0"

if not exist "%OUT%" (
    echo Downloading %URL% ...
    powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%URL%' -OutFile '%OUT%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
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
