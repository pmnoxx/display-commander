
#git submodule update --init --recursive --remote --merge
#git submodule update --remote --merge

param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Debug,
    [switch]$Release,
    [switch]$Help,
    [switch]$Experimental,
    [switch]$DebugSymbols
)

# Show help if requested
if ($Help) {
    Write-Host "Usage: .\bd_core.ps1 [BuildType] [Options]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Build Types:" -ForegroundColor Cyan
    Write-Host "  RelWithDebInfo (default) - Release with debug symbols" -ForegroundColor White
    Write-Host "  Debug                  - Full debug build" -ForegroundColor White
    Write-Host "  Release                - Optimized release build" -ForegroundColor White
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Cyan
    Write-Host "  -Debug                 - Force debug build" -ForegroundColor White
    Write-Host "  -Release               - Force release build" -ForegroundColor White
    Write-Host "  -DebugSymbols          - Force debug symbol generation (PDB) for all build types" -ForegroundColor White
    Write-Host "  -Experimental          - Enable experimental features (autoclick, time slowdown)" -ForegroundColor White
    Write-Host "  -Help                  - Show this help message" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Cyan
    Write-Host "  .\bd_core.ps1                    # Build with RelWithDebInfo (default)" -ForegroundColor White
    Write-Host "  .\bd_core.ps1 -Debug             # Build with Debug configuration" -ForegroundColor White
    Write-Host "  .\bd_core.ps1 -Release           # Build with Release configuration" -ForegroundColor White
    Write-Host "  .\bd_core.ps1 -Release -DebugSymbols  # Release build with debug symbols" -ForegroundColor White
    Write-Host "  .\bd_core.ps1 -Experimental     # Build with experimental features enabled" -ForegroundColor White
    Write-Host "  .\bd_core.ps1 Debug              # Build with Debug configuration" -ForegroundColor White
    exit 0
}

# Override BuildType based on switches
if ($Debug) {
    $BuildType = "Debug"
} elseif ($Release) {
    $BuildType = "Release"
}

Write-Host "Building with configuration: $BuildType" -ForegroundColor Green
if ($Experimental) {
    Write-Host "Experimental features: ENABLED" -ForegroundColor Yellow
} else {
    Write-Host "Experimental features: DISABLED" -ForegroundColor Gray
}
if ($DebugSymbols) {
    Write-Host "Debug symbols: FORCED (PDB will be generated)" -ForegroundColor Yellow
} else {
    Write-Host "Debug symbols: Default for build type" -ForegroundColor Gray
}

# Build the project with the specified configuration
# cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$BuildType -DEXPERIMENTAL_TAB=ON
# cmake --build build --config $BuildType

if ($Experimental) {
    if ($DebugSymbols) {
        ./build_display_commander.ps1 -BuildType $BuildType -Experimental -DebugSymbols
    } else {
        ./build_display_commander.ps1 -BuildType $BuildType -Experimental
    }
} else {
    if ($DebugSymbols) {
        ./build_display_commander.ps1 -BuildType $BuildType -DebugSymbols
    } else {
        ./build_display_commander.ps1 -BuildType $BuildType
    }
}
Copy-Item "build\src\addons\display_commander\zzz_display_commander.addon64" "build\zzz_display_commander.addon64" -Force

# Copy PDB file if it exists (for debugging crash dumps)
$pdbSource = "build\src\addons\display_commander\zzz_display_commander.pdb"
$pdbDest = "build\zzz_display_commander.pdb"
if (Test-Path $pdbSource) {
    Copy-Item $pdbSource $pdbDest -Force
    Write-Host "Debug symbols (PDB) copied to build directory" -ForegroundColor Cyan
} else {
    Write-Warning "PDB file not found at $pdbSource - debug symbols may not be available"
}
