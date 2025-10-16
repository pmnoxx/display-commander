@echo off
REM Build script for Enhanced ReShade Injector
REM This script builds the enhanced_injector.exe executable

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=RelWithDebInfo

echo Building Enhanced ReShade Injector with configuration: %BUILD_TYPE%...

REM Configure and build the enhanced injector
cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=%BUILD_TYPE%" -DEXPERIMENTAL_TAB=ON
cmake --build build --config "%BUILD_TYPE%" --target enhanced_injector

REM Check if build was successful
if %ERRORLEVEL% equ 0 (
    echo Enhanced ReShade Injector built successfully!

    REM Check if the output file exists
    set "OUTPUT_FILE=build\tools\enhanced_injector.exe"
    if exist "%OUTPUT_FILE%" (
        echo Output file: %OUTPUT_FILE%

        REM Check if config file was copied
        set "CONFIG_FILE=build\tools\injector_config.ini"
        if exist "%CONFIG_FILE%" (
            echo Configuration file: %CONFIG_FILE%
        ) else (
            echo Warning: Configuration file not found at expected location: %CONFIG_FILE%
        )
    ) else (
        echo Warning: Output file not found at expected location: %OUTPUT_FILE%
    )
) else (
    echo Build failed with exit code: %ERRORLEVEL%
    exit /b 1
)

echo Build completed!
echo.
echo Usage:
echo   .\build\tools\enhanced_injector.exe --help
echo   .\build\tools\enhanced_injector.exe
echo.
echo Configuration file: build\tools\injector_config.ini
