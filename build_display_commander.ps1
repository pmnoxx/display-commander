# Build script for Display Commander addon only
# This script builds only the zzz_display_commander.addon64 file

param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Experimental,
    [switch]$DebugSymbols
)

Write-Host "Building Display Commander addon with configuration: $BuildType..." -ForegroundColor Green

# Build CMake command with conditional experimental features flag
# Unset cached EXPERIMENTAL_FEATURES first to force update
$cmakeArgs = @(
    "-S", ".",
    "-B", "build",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DEXPERIMENTAL_TAB=ON",
    "-UEXPERIMENTAL_FEATURES"
)

if ($Experimental) {
    $cmakeArgs += "-DEXPERIMENTAL_FEATURES=ON"
    Write-Host "Experimental features: ENABLED" -ForegroundColor Yellow
} else {
    $cmakeArgs += "-DEXPERIMENTAL_FEATURES=OFF"
    Write-Host "Experimental features: DISABLED" -ForegroundColor Gray
}

if ($DebugSymbols) {
    $cmakeArgs += "-DFORCE_DEBUG_SYMBOLS=ON"
    Write-Host "Debug symbols: FORCED" -ForegroundColor Yellow
}

# Configure and build only the display commander addon
cmake $cmakeArgs
cmake --build build --config "$BuildType" --target zzz_display_commander

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "Display Commander addon built successfully!" -ForegroundColor Green

    # Check if the output file exists
    $outputFile = "build\src\addons\display_commander\zzz_display_commander.addon64"
    if (Test-Path $outputFile) {
        Write-Host "Output file: $outputFile" -ForegroundColor Cyan
        $fileSize = (Get-Item $outputFile).Length
        Write-Host "File size: $([math]::Round($fileSize / 1KB, 2)) KB" -ForegroundColor Cyan
    } else {
        Write-Warning "Output file not found at expected location: $outputFile"
    }
} else {
    Write-Error "Build failed with exit code: $LASTEXITCODE"
    exit 1
}

Write-Host "Build completed!" -ForegroundColor Green
