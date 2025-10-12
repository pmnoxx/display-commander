#!/bin/bash

#git submodule update --init --recursive --remote --merge
#git submodule update --remote --merge

# Default build type
BUILD_TYPE="RelWithDebInfo"
DEBUG=false
RELEASE=false
HELP=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -Debug|--debug)
            DEBUG=true
            shift
            ;;
        -Release|--release)
            RELEASE=true
            shift
            ;;
        -Help|--help|-h)
            HELP=true
            shift
            ;;
        Debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        Release)
            BUILD_TYPE="Release"
            shift
            ;;
        RelWithDebInfo)
            BUILD_TYPE="RelWithDebInfo"
            shift
            ;;
        *)
            BUILD_TYPE="$1"
            shift
            ;;
    esac
done

# Show help if requested
if [ "$HELP" = true ]; then
    echo -e "\033[33mUsage: ./bd.sh [BuildType] [Options]\033[0m"
    echo ""
    echo -e "\033[36mBuild Types:\033[0m"
    echo -e "\033[37m  RelWithDebInfo (default) - Release with debug symbols\033[0m"
    echo -e "\033[37m  Debug                  - Full debug build\033[0m"
    echo -e "\033[37m  Release                - Optimized release build\033[0m"
    echo ""
    echo -e "\033[36mOptions:\033[0m"
    echo -e "\033[37m  -Debug                 - Force debug build\033[0m"
    echo -e "\033[37m  -Release               - Force release build\033[0m"
    echo -e "\033[37m  -Help                  - Show this help message\033[0m"
    echo ""
    echo -e "\033[36mExamples:\033[0m"
    echo -e "\033[37m  ./bd.sh                    # Build with RelWithDebInfo (default)\033[0m"
    echo -e "\033[37m  ./bd.sh -Debug             # Build with Debug configuration\033[0m"
    echo -e "\033[37m  ./bd.sh -Release           # Build with Release configuration\033[0m"
    echo -e "\033[37m  ./bd.sh Debug              # Build with Debug configuration\033[0m"
    exit 0
fi

# Override BuildType based on switches
if [ "$DEBUG" = true ]; then
    BUILD_TYPE="Debug"
elif [ "$RELEASE" = true ]; then
    BUILD_TYPE="Release"
fi

echo -e "\033[32mBuilding with configuration: $BUILD_TYPE\033[0m"

# Build the project with the specified configuration
# cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DEXPERIMENTAL_TAB=ON
# cmake --build build --config $BUILD_TYPE

./build_display_commander.sh "$BUILD_TYPE"
cp "build/src/addons/display_commander/zzz_display_commander.addon64" "build/zzz_display_commander.addon64"

copy_railgrade_files() {
    local addon_name="$1"
    local target_directory="$2"

    if [ -f "build/zzz_display_commander.addon64" ]; then
        rm -f "$target_directory/zzz_display_commander.asi"
        cp "build/zzz_display_commander.addon64" "$target_directory/zzz_display_commander.addon64"
        rm -f "$target_directory/renodx-display_commander.addon64"
        rm -f "$target_directory/_display_commander.addon64"
    fi
    rm -f "$target_directory/d3d_debug_layer.addon64"
}

# Copy files to various game directories
copy_railgrade_files "_yohane" "/c/Program Files (x86)/Steam/steamapps/common/Yohanuma"

copy_railgrade_files "_p5px" "/c/Program Files (x86)/Steam/steamapps/common/P5X/client/pc"

copy_railgrade_files "_mary_skelter_finale" "/d/SteamLibrary/steamapps/common/Mary Skelter Finale"

copy_railgrade_files "_hbr" "/c/Program Files (x86)/Steam/steamapps/common/HeavenBurnsRed"

copy_railgrade_files "_p5s" "/e/SteamLibrary/steamapps/common/P5S"

copy_railgrade_files "_ksp" "/c/Program Files (x86)/Steam/steamapps/common/Kerbal Space Program"

copy_railgrade_files "_darkestdungeon2" "/d/SteamLibrary/steamapps/common/Darkest Dungeon® II"

copy_railgrade_files "_againstthestorm" "/d/SteamLibrary/steamapps/common/Against the Storm"

copy_railgrade_files "_madodora" "/c/Program Files (x86)/Steam/steamapps/common/MadokaExedra"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/manosaba_game"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Muv-Luv Alternative Total Eclipse"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Gotchi Guardians"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/No Creeps Were Harmed TD"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/BlazblueEntropyEffect"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Rain World"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Nunholy"
copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/The Last Spell"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Hardspace Shipbreaker"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Artisan TD"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/No Creeps Were Harmed TD"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/Phantom Brigade"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/SlayTheSpire"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/Chrono Ark/x64/Master"

copy_railgrade_files "unrealengine" "/d/SteamLibrary/steamapps/common/Octopath_Traveler2/Octopath_Traveler2/Binaries/Win64"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/IXION"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/This War of Mine/x64"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Barbarian Saga The Beastmaster Playtest"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/BlueArchive"
copy_railgrade_files "unrealengine" "/c/Program Files (x86)/Steam/steamapps/common/SumerianSixDemo/ArtificerTwo/Binaries/Win64"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/Dungeon Warfare 3 Demo"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Sekiro"
copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/RESIDENT EVIL 2  BIOHAZARD RE2"
copy_railgrade_files "_univ" "/e/SteamLibrary/steamapps/common/1000xRESIST"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Dungeon Warfare 3"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/The Hundred Line -Last Defense Academy-"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/ELDEN RING/Game"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Hollow Knight"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/VirtuaUnlimitedProject/addons"

copy_railgrade_files "unrealengine" "/c/Program Files (x86)/Steam/steamapps/common/StellarBlade/SB/Binaries/Win64"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Path of Exile 2"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/The Alters/TheAlters/Binaries/Win64"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Bloodecay"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/stasis2"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Bloodecay"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/AI Limit"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/WhiskerwoodDemo/Whiskerwood/Binaries/Win64"

copy_railgrade_files "_univ" "/c/ProgramData/Smilegate/Games/ChaosZeroNightmare/bin"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Returnal/Returnal/Binaries/Win64"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Total War WARHAMMER III"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/HITMAN 3/Retail"

copy_railgrade_files "_univ" "/d/SteamLibrary/steamapps/common/Godstrike"

copy_railgrade_files "_univ" "/c/Program Files (x86)/Steam/steamapps/common/Blue Protocol Star Resonance/bpsr"
