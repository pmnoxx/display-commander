
#git submodule update --init --recursive --remote --merge
#git submodule update --remote --merge

param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Debug,
    [switch]$Release,
    [switch]$Help,
    [switch]$Experimental
)

# Show help if requested
if ($Help) {
    Write-Host "Usage: .\bd32_core.ps1 [BuildType] [Options]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Build Types:" -ForegroundColor Cyan
    Write-Host "  RelWithDebInfo (default) - Release with debug symbols" -ForegroundColor White
    Write-Host "  Debug                  - Full debug build" -ForegroundColor White
    Write-Host "  Release                - Optimized release build" -ForegroundColor White
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Cyan
    Write-Host "  -Debug                 - Force debug build" -ForegroundColor White
    Write-Host "  -Release               - Force release build" -ForegroundColor White
    Write-Host "  -Experimental          - Enable experimental features (autoclick, time slowdown)" -ForegroundColor White
    Write-Host "  -Help                  - Show this help message" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Cyan
    Write-Host "  .\bd32_core.ps1                    # Build with RelWithDebInfo (default)" -ForegroundColor White
    Write-Host "  .\bd32_core.ps1 -Debug             # Build with Debug configuration" -ForegroundColor White
    Write-Host "  .\bd32_core.ps1 -Release           # Build with Release configuration" -ForegroundColor White
    Write-Host "  .\bd32_core.ps1 -Experimental     # Build with experimental features enabled" -ForegroundColor White
    Write-Host "  .\bd32_core.ps1 Debug              # Build with Debug configuration" -ForegroundColor White
    Write-Host ""
    Write-Host "Note: This script builds 32-bit (.addon32) version of Display Commander" -ForegroundColor Yellow
    exit 0
}

# Override BuildType based on switches
if ($Debug) {
    $BuildType = "Debug"
} elseif ($Release) {
    $BuildType = "Release"
}

Write-Host "Building 32-bit version with configuration: $BuildType" -ForegroundColor Green
if ($Experimental) {
    Write-Host "Experimental features: ENABLED" -ForegroundColor Yellow
} else {
    Write-Host "Experimental features: DISABLED" -ForegroundColor Gray
}

# Build the project with the specified configuration for 32-bit
# cmake -S . -B build32 -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=$BuildType -DEXPERIMENTAL_TAB=ON
# cmake --build build32 --config $BuildType

# Configure for 32-bit using Visual Studio 2022 generator
# Unset cached EXPERIMENTAL_FEATURES first to force update
$cmakeArgs = @("-S", ".", "-B", "build32", "-G", "Visual Studio 17 2022", "-A", "Win32", "-DCMAKE_BUILD_TYPE=$BuildType", "-DEXPERIMENTAL_TAB=ON", "-UEXPERIMENTAL_FEATURES")
if ($Experimental) {
    $cmakeArgs += "-DEXPERIMENTAL_FEATURES=ON"
} else {
    $cmakeArgs += "-DEXPERIMENTAL_FEATURES=OFF"
}
cmake @cmakeArgs

# Build
cmake --build build32 --config "$BuildType" --parallel

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "32-bit build completed successfully!" -ForegroundColor Green

    # Find the 32-bit addon file
    $addon32File = Get-ChildItem -Path "build32\src" -Recurse -Name "zzz_display_commander.addon32" | Select-Object -First 1
    if ($addon32File) {
        $sourcePath = "build32\src\$addon32File"
        Copy-Item $sourcePath "build32\zzz_display_commander.addon32" -Force
        Write-Host "Copied 32-bit addon to: build32\zzz_display_commander.addon32" -ForegroundColor Cyan

        #$fileSize = (Get-Item "build32\zzz_display_commander.addon32").Length
        #Write-Host "File size: $([math]::Round($fileSize / 1KB, 2)) KB" -ForegroundColor Cyan
    } else {
        Write-Warning "32-bit addon file not found in build32 directory"
    }
} else {
    Write-Error "32-bit build failed with exit code: $LASTEXITCODE"
    exit 1
}
