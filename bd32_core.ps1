
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

$parallelJobs = if ($env:CMAKE_BUILD_PARALLEL_LEVEL) { $env:CMAKE_BUILD_PARALLEL_LEVEL } else { $env:NUMBER_OF_PROCESSORS }
if (-not $parallelJobs) { $parallelJobs = 32 }
Write-Host "Building with $parallelJobs parallel job(s)" -ForegroundColor Gray

# Prefer Ninja preset (parallel per .cpp like renodx2) when CMakePresets.json exists and we can run 32-bit env
$useNinjaPreset = $false
$vcvars32 = $null
if (Test-Path "$PSScriptRoot\CMakePresets.json") {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vsPath) {
            $vcvars32 = Join-Path $vsPath "VC\Auxiliary\Build\vcvars32.bat"
            if (Test-Path $vcvars32) { $useNinjaPreset = $true }
        }
    }
}

if ($useNinjaPreset) {
    Write-Host "Using Ninja preset (parallel build)" -ForegroundColor Cyan
    $experimentalFlag = if ($Experimental) { "-DEXPERIMENTAL_FEATURES=ON" } else { "-DEXPERIMENTAL_FEATURES=OFF" }
    $configureCmd = "cmake --preset ninja-x86 -DCMAKE_BUILD_TYPE=$BuildType $experimentalFlag"
    $buildCmd = "cmake --build build32 --parallel $parallelJobs"
    $batchCmd = "call `"$vcvars32`" && cd /d `"$PSScriptRoot`" && $configureCmd && $buildCmd"
    cmd /c $batchCmd
} else {
    # Fallback: Visual Studio 2022 generator (enable intra-project parallelism via CL_MPCount)
    if (-not $useNinjaPreset -and (Test-Path "$PSScriptRoot\CMakePresets.json")) {
        Write-Host "Ninja preset available but vcvars32.bat not found; using Visual Studio generator" -ForegroundColor Gray
    }
    $build32Dir = Join-Path $PSScriptRoot "build32"
    $cmakeArgs = @("-S", $PSScriptRoot, "-B", $build32Dir, "-G", "Visual Studio 17 2022", "-A", "Win32", "-DCMAKE_BUILD_TYPE=$BuildType", "-DEXPERIMENTAL_TAB=ON", "-UEXPERIMENTAL_FEATURES")
    if ($Experimental) {
        $cmakeArgs += "-DEXPERIMENTAL_FEATURES=ON"
    } else {
        $cmakeArgs += "-DEXPERIMENTAL_FEATURES=OFF"
    }
    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    cmake --build $build32Dir --config "$BuildType" --parallel $parallelJobs -- /p:CL_MPCount=$parallelJobs
}

$build32Dir = Join-Path $PSScriptRoot "build32"
# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "32-bit build completed successfully!" -ForegroundColor Green

    # Find the 32-bit addon (Ninja: build32\... or build32\src\...; VS: build32\src\...)
    $addonPath = Get-ChildItem -Path $build32Dir -Recurse -Filter "zzz_display_commander.addon32" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
    if ($addonPath) {
        $destPath = Join-Path $build32Dir "zzz_display_commander.addon32"
        Copy-Item $addonPath $destPath -Force
        Write-Host "Copied 32-bit addon to: $destPath" -ForegroundColor Cyan
    }
    # Copy PDB from build32\src\addons\display_commander to build32 so debugger finds symbols
    $pdbSource = Join-Path $build32Dir "src\addons\display_commander\zzz_display_commander.pdb"
    if (Test-Path $pdbSource) {
        Copy-Item $pdbSource (Join-Path $build32Dir "zzz_display_commander.pdb") -Force
        Write-Host "Copied PDB to: build32\zzz_display_commander.pdb" -ForegroundColor Cyan
    }
    if (-not $addonPath) {
        Write-Warning "32-bit addon file not found in build32 directory"
    }
} else {
    Write-Error "32-bit build failed with exit code: $LASTEXITCODE"
    exit 1
}
