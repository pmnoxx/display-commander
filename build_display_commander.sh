#!/bin/bash

# Build script for Display Commander addon only
# This script builds only the zzz_display_commander.addon64 file

BUILD_TYPE="${1:-RelWithDebInfo}"

echo -e "\033[32mBuilding Display Commander addon with configuration: $BUILD_TYPE...\033[0m"

# Configure and build only the display commander addon
cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" -DEXPERIMENTAL_TAB=ON
cmake --build build --config "$BUILD_TYPE" --target zzz_display_commander

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "\033[32mDisplay Commander addon built successfully!\033[0m"

    # Check if the output file exists
    output_file="build/src/addons/display_commander/zzz_display_commander.addon64"
    if [ -f "$output_file" ]; then
        echo -e "\033[36mOutput file: $output_file\033[0m"
        file_size=$(stat -c%s "$output_file" 2>/dev/null || stat -f%z "$output_file" 2>/dev/null)
        if [ $? -eq 0 ]; then
            file_size_kb=$((file_size / 1024))
            echo -e "\033[36mFile size: ${file_size_kb} KB\033[0m"
        fi
    else
        echo -e "\033[33mWarning: Output file not found at expected location: $output_file\033[0m"
    fi
else
    echo -e "\033[31mError: Build failed with exit code: $?\033[0m"
    exit 1
fi

echo -e "\033[32mBuild completed!\033[0m"
