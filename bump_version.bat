@echo off
REM Display Commander Version Bumper - Batch Wrapper
REM This is a simple wrapper for the PowerShell script

if "%1"=="" (
    echo Usage: bump_version.bat [major^|minor^|patch^|build] [options]
    echo.
    echo Options:
    echo   --build    Build the project after version bump
    echo   --commit   Commit changes to git
    echo   --tag      Create git tag
    echo   --message  Custom commit message
    echo.
    echo Examples:
    echo   bump_version.bat patch --build --commit
    echo   bump_version.bat minor --build --commit --tag
    echo   bump_version.bat build --build
    echo.
    pause
    exit /b 1
)

powershell -ExecutionPolicy Bypass -File "%~dp0bump_version.ps1" %*
