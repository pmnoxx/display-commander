
#git submodule update --init --recursive --remote --merge
#git submodule update --remote --merge

param(
    [string]$BuildType = "RelWithDebInfo",
    [switch]$Debug,
    [switch]$Release,
    [switch]$Help
)

# Show help if requested
if ($Help) {
    Write-Host "Usage: .\bd32.ps1 [BuildType] [Options]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Build Types:" -ForegroundColor Cyan
    Write-Host "  RelWithDebInfo (default) - Release with debug symbols" -ForegroundColor White
    Write-Host "  Debug                  - Full debug build" -ForegroundColor White
    Write-Host "  Release                - Optimized release build" -ForegroundColor White
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Cyan
    Write-Host "  -Debug                 - Force debug build" -ForegroundColor White
    Write-Host "  -Release               - Force release build" -ForegroundColor White
    Write-Host "  -Help                  - Show this help message" -ForegroundColor White
    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Cyan
    Write-Host "  .\bd32.ps1                    # Build with RelWithDebInfo (default)" -ForegroundColor White
    Write-Host "  .\bd32.ps1 -Debug             # Build with Debug configuration" -ForegroundColor White
    Write-Host "  .\bd32.ps1 -Release           # Build with Release configuration" -ForegroundColor White
    Write-Host "  .\bd32.ps1 Debug              # Build with Debug configuration" -ForegroundColor White
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

# Build the project with the specified configuration for 32-bit
# cmake -S . -B build32 -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=$BuildType -DEXPERIMENTAL_TAB=ON
# cmake --build build32 --config $BuildType

# Configure for 32-bit using Visual Studio 2022 generator
cmake -S . -B build32 -G "Visual Studio 17 2022" -A Win32 "-DCMAKE_BUILD_TYPE=$BuildType" -DEXPERIMENTAL_TAB=ON

# Build
cmake --build build32 --config "$BuildType" --parallel

# Check if build was successful
if ($LASTEXITCODE -eq 0) {
    Write-Host "32-bit build completed successfully!" -ForegroundColor Green

    # Find the 32-bit addon file
    $addon32File = Get-ChildItem -Path "build32" -Recurse -Name "zzz_display_commander.addon32" | Select-Object -First 1
    if ($addon32File) {
        $sourcePath = "build32\$addon32File"
        Copy-Item $sourcePath "build32\zzz_display_commander.addon32" -Force
        Write-Host "Copied 32-bit addon to: build32\zzz_display_commander.addon32" -ForegroundColor Cyan

        $fileSize = (Get-Item "build32\zzz_display_commander.addon32").Length
        Write-Host "File size: $([math]::Round($fileSize / 1KB, 2)) KB" -ForegroundColor Cyan
    } else {
        Write-Warning "32-bit addon file not found in build32 directory"
    }
} else {
    Write-Error "32-bit build failed with exit code: $LASTEXITCODE"
    exit 1
}

function Copy-RailgradeFiles32 {
    param(
        [string]$AddonName,
        [string]$TargetDirectory
    )
    if (Test-Path "build32\zzz_display_commander.addon32") {
        Remove-Item "$TargetDirectory\zzz_display_commander.asi" -Force -ErrorAction SilentlyContinue
        Copy-Item "build32\zzz_display_commander.addon32" "$TargetDirectory\zzz_display_commander.addon32" -Force
        Remove-Item "$TargetDirectory\renodx-display_commander.addon32" -Force -ErrorAction SilentlyContinue
        Remove-Item "$TargetDirectory\_display_commander.addon32" -Force -ErrorAction SilentlyContinue
    }
    Remove-Item "$TargetDirectory\d3d_debug_layer.addon32" -Force -ErrorAction SilentlyContinue
}


Copy-RailgradeFiles32 -AddonName "_yohane" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Yohanuma"

Copy-RailgradeFiles32 -AddonName "_p5px" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\P5X\client\pc"

Copy-RailgradeFiles32 -AddonName "_mary_skelter_finale" -TargetDirectory "D:\SteamLibrary\steamapps\common\Mary Skelter Finale"

Copy-RailgradeFiles32 -AddonName "_hbr" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\HeavenBurnsRed"

Copy-RailgradeFiles32 -AddonName "_p5s" -TargetDirectory "E:\SteamLibrary\steamapps\common\P5S"

Copy-RailgradeFiles32 -AddonName "_ksp" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Kerbal Space Program"

Copy-RailgradeFiles32 -AddonName "_darkestdungeon2" -TargetDirectory "D:\SteamLibrary\steamapps\common\Darkest Dungeon® II"

Copy-RailgradeFiles32 -AddonName "_againstthestorm" -TargetDirectory "D:\SteamLibrary\steamapps\common\Against the Storm"

Copy-RailgradeFiles32 -AddonName "_madodora" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\MadokaExedra"



Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\manosaba_game"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Muv-Luv Alternative Total Eclipse"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Gotchi Guardians"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\No Creeps Were Harmed TD"



Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\BlazblueEntropyEffect"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Rain World"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Nunholy"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\The Last Spell"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Hardspace Shipbreaker"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Artisan TD"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\No Creeps Were Harmed TD"




Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\Phantom Brigade"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\SlayTheSpire"



Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\Chrono Ark\x64\Master"

Copy-RailgradeFiles32 -AddonName "unrealengine" -TargetDirectory "D:\SteamLibrary\steamapps\common\Octopath_Traveler2\Octopath_Traveler2\Binaries\Win64"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\IXION"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\This War of Mine\x64"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Barbarian Saga The Beastmaster Playtest"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\BlueArchive"
Copy-RailgradeFiles32 -AddonName "unrealengine" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\SumerianSixDemo\ArtificerTwo\Binaries\Win64"



Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\Dungeon Warfare 3 Demo"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Sekiro"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\RESIDENT EVIL 2  BIOHAZARD RE2"
Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "E:\SteamLibrary\steamapps\common\1000xRESIST"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Dungeon Warfare 3"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\The Hundred Line -Last Defense Academy-"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\ELDEN RING\Game"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Hollow Knight"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\VirtuaUnlimitedProject\addons"


Copy-RailgradeFiles32 -AddonName "unrealengine" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile 2"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\The Alters\TheAlters\Binaries\Win64"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Bloodecay"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\stasis2"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Bloodecay"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\AI Limit"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\WhiskerwoodDemo\Whiskerwood\Binaries\Win64"



Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\ProgramData\Smilegate\Games\ChaosZeroNightmare\bin"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Returnal\Returnal\Binaries\Win64"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\Total War WARHAMMER III"


Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3\Retail"

Copy-RailgradeFiles32 -AddonName "_univ" -TargetDirectory "D:\SteamLibrary\steamapps\common\Godstrike"
