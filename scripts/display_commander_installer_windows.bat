@echo off
Setlocal EnableDelayedExpansion

:: Display Commander + ReShade installer for Windows
:: Downloads ReShade and Display Commander addon (64 or 32-bit), installs to game folder.

::Begin user tweakables

::Leave blank to be prompted for 64/32-bit
set "bit="

:: Display Commander addon URLs (latest release)
set "addonUrl64=https://github.com/pmnoxx/display-commander/releases/latest/download/zzz_display_commander.addon64"
set "addonName64=zzz_display_commander.addon64"

set "addonUrl32=https://github.com/pmnoxx/display-commander/releases/latest/download/zzz_display_commander.addon32"
set "addonName32=zzz_display_commander.addon32"

::Leave blank if you want to be prompted for an install path (set gameFolder=%cd% will use the directory the script is in)
set "gameFolder="

::Edit this if you want to use localized installations without symlinks
set "centralized=true"
set "centralizedFolder=%LOCALAPPDATA%\Programs\Display Commander"

:: DO NOT EDIT BELOW
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

if "%centralized%"=="true" (
    :: Check privileges (symlinks may require admin)
    net file 1>NUL 2>NUL
    if errorlevel 1 (
        powershell Start-Process -FilePath "%~f0" -ArgumentList "%cd%" -Verb RunAs >NUL 2>&1
        exit /b
    )

    :: Change directory with passed argument
    if not "%~1"=="" cd /d "%~1"

    :: Ensure centralized folder exists
    if not exist "!centralizedFolder!" (
        mkdir "!centralizedFolder!"
    )
    cd /d "!centralizedFolder!"
)

if "%bit%"=="" (
    choice /m "Is the game 64-bit? "
    if errorlevel 2 (
        set "bit=32"
    ) else (
        set "bit=64"
    )
)

if "%bit%"=="64" (
    set "addonUrl=%addonUrl64%"
    set "addonName=%addonName64%"
    set "reshadeName=ReShade64.dll"
) else (
    set "addonUrl=%addonUrl32%"
    set "addonName=%addonName32%"
    set "reshadeName=ReShade32.dll"
)

if "%gameFolder%"=="" (
    echo Enter the game's directory:
    set /p "gameFolder="
)

echo.
echo Choose ReShade DLL name (API the game uses):
choice /c 12345 /m "1=dxgi  2=d3d9  3=d3d11  4=d3d12  5=opengl32"
if errorlevel 5 set "reshadeNameDll=opengl32.dll"
if errorlevel 4 set "reshadeNameDll=d3d12.dll"
if errorlevel 3 set "reshadeNameDll=d3d11.dll"
if errorlevel 2 set "reshadeNameDll=d3d9.dll"
if errorlevel 1 set "reshadeNameDll=dxgi.dll"
echo Using: %reshadeNameDll%
echo.

if "%centralized%"=="false" (
    cd /d "%gameFolder%"
    set "reshadeExists=%reshadeNameDll%"
) else (
    set "reshadeExists=%reshadeName%"
)

:: ---- ReShade ----
set "downloadReshade=true"
if exist "%reshadeExists%" (
    choice /m "ReShade already installed, would you like to update? "
    if errorlevel 2 (
        set "downloadReshade=false"
    )
)

if "%downloadReshade%"=="true" (
    echo Downloading latest ReShade...
    powershell -Command Invoke-WebRequest -Uri "https://reshade.me/downloads/ReShade_Setup_Addon.exe" -OutFile "reshade.exe"

    echo Extracting ReShade...
    if "%centralized%"=="true" (
        tar -xf "reshade.exe" ReShade64.dll
        tar -xf "reshade.exe" ReShade32.dll
    ) else (
        tar -xf "reshade.exe" "%reshadeName%"
        move /Y "%reshadeName%" "%gameFolder%\%reshadeNameDll%"
    )

    echo Cleaning up ReShade files...
    del reshade.exe
)

if "%centralized%"=="true" (
    echo Symlinking "%reshadeName%" to game directory
    if exist "%gameFolder%\%reshadeNameDll%" (
        echo File already exists and will be overwritten...
        del "%gameFolder%\%reshadeNameDll%"
    )
    mklink "%gameFolder%\%reshadeNameDll%" "%centralizedFolder%\%reshadeName%"
)

:: ---- ReShade.ini ----
if not exist "%gameFolder%\ReShade.ini" (
    echo Creating ReShade.ini...
    echo [OVERLAY] >> "%gameFolder%\ReShade.ini"
    echo TutorialProgress=4 >> "%gameFolder%\ReShade.ini"
    echo [ADDON] >> "%gameFolder%\ReShade.ini"
    echo DisabledAddons=Effect Runtime Sync,Generic Depth >> "%gameFolder%\ReShade.ini"
) else (
    echo ReShade.ini already exists.
)

:: ---- Display Commander addon ----
set "downloadAddon=true"
if exist "%gameFolder%\%addonName%" (
    choice /m "Display Commander addon already installed, would you like to update? "
    if errorlevel 2 (
        set "downloadAddon=false"
    )
)

if "%downloadAddon%"=="true" (
    echo Downloading Display Commander addon...
    if "%centralized%"=="true" (
        powershell -Command Invoke-WebRequest -Uri "%addonUrl%" -OutFile "%addonName%"
    ) else (
        powershell -Command Invoke-WebRequest -Uri "%addonUrl%" -OutFile "%gameFolder%\%addonName%"
    )
)

if "%centralized%"=="true" (
    if exist "%centralizedFolder%\%addonName%" (
        echo Symlinking "%addonName%" to game directory
        if exist "%gameFolder%\%addonName%" (
            echo File already exists and will be overwritten...
            del "%gameFolder%\%addonName%"
        )
        mklink "%gameFolder%\%addonName%" "%centralizedFolder%\%addonName%"
    ) else (
        echo Skipping addon symlink: "%addonName%" not found in central folder.
    )
) else (
    :: Addon already downloaded to game folder when downloadAddon was true
)

echo.
echo ReShade and Display Commander have been installed.
echo Game folder: %gameFolder%
echo ReShade DLL: %reshadeNameDll%
echo Addon: %addonName%
echo.
echo Unless you see red errors above, you are done^^!

pause
