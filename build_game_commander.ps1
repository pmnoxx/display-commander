#!/usr/bin/env pwsh

# Build and run Game Commander
Write-Host "Building Game Commander..." -ForegroundColor Green

# Create build directory
if (!(Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

# Configure and build
Set-Location build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja game_commander

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build successful! Running Game Commander..." -ForegroundColor Green
    Set-Location ..
    .\build\tools\game_commander\game_commander.exe
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}
