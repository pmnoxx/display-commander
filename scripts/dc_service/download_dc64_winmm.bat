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
set "INSTALLER64=%LOCAL_DIR%\winmm.dll"

:: Shared ReShade setup folder (avoid redownload); Reshade64.dll in script folder when needed
set "RESHADE_SETUP_DIR=%LOCAL_DIR%\..\..\Display_Commander\Reshade_Setup"
set "RESHADE_EXE=ReShade_Setup_6.7.3_Addon.exe"
set "RESHADE_URL=https://reshade.me/downloads/ReShade_Setup_6.7.3_Addon.exe"
set "RESHADE_EXE_PATH=%RESHADE_SETUP_DIR%\%RESHADE_EXE%"
set "RESHADE_EXTRACT_DIR=%RESHADE_SETUP_DIR%\6.7.3"
set "LOCAL_RESHADE64=%LOCAL_DIR%\Reshade64.dll"

if not exist "%RESHADE_SETUP_DIR%" mkdir "%RESHADE_SETUP_DIR%"
if not exist "%RESHADE_EXE_PATH%" (
    echo Downloading %RESHADE_EXE% to Display_Commander\Reshade_Setup ...
    where curl >nul 2>&1
    if !errorlevel! equ 0 (
        curl -L -s -S -o "%RESHADE_EXE_PATH%" "%RESHADE_URL%"
    ) else (
        powershell -NoProfile -Command "try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%RESHADE_URL%' -OutFile '%RESHADE_EXE_PATH%' -UseBasicParsing } catch { Write-Host 'Download failed:' $_.Exception.Message; exit 1 }"
    )
    if errorlevel 1 (
        echo ReShade installer download failed.
        exit /b 1
    )
    echo Saved as %RESHADE_EXE_PATH%
) else (
    echo Using existing %RESHADE_EXE% in Display_Commander\Reshade_Setup
)

if not exist "%LOCAL_RESHADE64%" (
    if not exist "%RESHADE_EXTRACT_DIR%\Reshade64.dll" (
        echo Extracting Reshade64.dll from installer...
        if not exist "%RESHADE_EXTRACT_DIR%" mkdir "%RESHADE_EXTRACT_DIR%"
        pushd "%RESHADE_EXTRACT_DIR%"
        tar -xf "..\%RESHADE_EXE%" ReShade64.dll
        popd
        if not exist "%RESHADE_EXTRACT_DIR%\Reshade64.dll" (
            echo Failed to extract Reshade64.dll
            exit /b 1
        )
    )
    copy /Y "%RESHADE_EXTRACT_DIR%\Reshade64.dll" "%LOCAL_RESHADE64%" >nul
    echo Reshade64.dll copied to script folder.
) else (
    echo Using existing Reshade64.dll in script folder
)

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
move /Y "%LOCAL64%" "%INSTALLER64%" >nul
if errorlevel 1 (
    echo Failed to copy %OUT64% to dc_installer64.dll
    exit /b 1
)


