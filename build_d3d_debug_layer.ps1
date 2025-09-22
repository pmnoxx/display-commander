# Build script for D3D Debug Layer addon only
# This script builds only the zzz_dxgi_debug_layer.addon64 file

Write-Host "Building D3D Debug Layer addon..." -ForegroundColor Green

# Configure and build only the D3D debug layer addon
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEXPERIMENTAL_TAB=ON
cmake --build build --config RelWithDebInfo --target zzz_dxgi_debug_layer

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "D3D Debug Layer addon built successfully!" -ForegroundColor Green

    # Check if the output file exists
    $outputFile = "build\src\addons\d3d_debug_layer\zzz_dxgi_debug_layer.addon64"
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
