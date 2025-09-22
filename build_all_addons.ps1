# Build script for all addons
# This script builds both the display commander and D3D debug layer addons

Write-Host "Building all addons..." -ForegroundColor Green

# Configure and build all addons
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEXPERIMENTAL_TAB=ON
cmake --build build --config RelWithDebInfo

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "All addons built successfully!" -ForegroundColor Green

    # Check display commander addon
    $displayCommanderFile = "build\src\addons\display_commander\zzz_display_commander.addon64"
    if (Test-Path $displayCommanderFile) {
        $fileSize = (Get-Item $displayCommanderFile).Length
        Write-Host "Display Commander: $displayCommanderFile ($([math]::Round($fileSize / 1KB, 2)) KB)" -ForegroundColor Cyan
    } else {
        Write-Warning "Display Commander addon not found at: $displayCommanderFile"
    }

    # Check D3D debug layer addon
    $d3dDebugFile = "build\src\addons\d3d_debug_layer\zzz_dxgi_debug_layer.addon64"
    if (Test-Path $d3dDebugFile) {
        $fileSize = (Get-Item $d3dDebugFile).Length
        Write-Host "D3D Debug Layer: $d3dDebugFile ($([math]::Round($fileSize / 1KB, 2)) KB)" -ForegroundColor Cyan
    } else {
        Write-Warning "D3D Debug Layer addon not found at: $d3dDebugFile"
    }
} else {
    Write-Error "Build failed with exit code: $LASTEXITCODE"
    exit 1
}

Write-Host "Build completed!" -ForegroundColor Green
