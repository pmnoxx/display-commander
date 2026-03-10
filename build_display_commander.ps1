# Build script for Display Commander addon only
# This script builds the zzz_display_commander.addon64 addon and Display Commander Launcher.exe standalone.
# Uses MSVC (Ninja + vcvars64) when available; falls back to Visual Studio generator.

param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Experimental,
    [switch]$DebugSymbols
)

Write-Host "Building Display Commander addon with configuration: $BuildType (MSVC)..." -ForegroundColor Green

# Prefer Ninja + MSVC preset when CMakePresets.json exists and vcvars64 is available
$useNinjaPreset = $false
$vcvars64 = $null
if (Test-Path "$PSScriptRoot\CMakePresets.json") {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vsPath) {
            $vcvars64 = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $vcvars64) { $useNinjaPreset = $true }
        }
    }
}

$experimentalFlag = if ($Experimental) { "-DEXPERIMENTAL_FEATURES=ON" } else { "-DEXPERIMENTAL_FEATURES=OFF" }
if ($Experimental) {
    Write-Host "Experimental features: ENABLED" -ForegroundColor Yellow
} else {
    Write-Host "Experimental features: DISABLED" -ForegroundColor Gray
}

if ($DebugSymbols) {
    Write-Host "Debug symbols: FORCED" -ForegroundColor Yellow
}

if ($useNinjaPreset) {
    Write-Host "Using Ninja preset (MSVC)" -ForegroundColor Cyan
    $configureExtra = "-DCMAKE_BUILD_TYPE=$BuildType $experimentalFlag"
    if ($DebugSymbols) { $configureExtra += " -DFORCE_DEBUG_SYMBOLS=ON" }
    $configureCmd = "cmake --preset ninja-x64 $configureExtra"
    $buildCmd = "cmake --build build --config $BuildType --target zzz_display_commander display_commander_exe"
    $batchCmd = "call `"$vcvars64`" && cd /d `"$PSScriptRoot`" && $configureCmd && $buildCmd"
    cmd /c $batchCmd
} else {
    if (Test-Path "$PSScriptRoot\CMakePresets.json") {
        Write-Host "Ninja preset available but vcvars64.bat not found; using Visual Studio generator" -ForegroundColor Gray
    }
    $cmakeArgs = @(
        "-S", ".",
        "-B", "build",
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DEXPERIMENTAL_TAB=ON",
        "-UEXPERIMENTAL_FEATURES"
    )
    $cmakeArgs += $experimentalFlag
    if ($DebugSymbols) { $cmakeArgs += "-DFORCE_DEBUG_SYMBOLS=ON" }
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build build --config "$BuildType" --target zzz_display_commander display_commander_exe
}

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
