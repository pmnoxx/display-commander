#!/bin/bash
# Build script for Enhanced ReShade Injector
# This script builds the enhanced_injector.exe executable

set -e

BUILD_TYPE=${1:-"RelWithDebInfo"}

echo "Building Enhanced ReShade Injector with configuration: $BUILD_TYPE..."

# Configure and build the enhanced injector
cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" -DEXPERIMENTAL_TAB=ON
cmake --build build --config "$BUILD_TYPE" --target enhanced_injector

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "Enhanced ReShade Injector built successfully!"

    # Check if the output file exists
    OUTPUT_FILE="build/tools/enhanced_injector.exe"
    if [ -f "$OUTPUT_FILE" ]; then
        echo "Output file: $OUTPUT_FILE"
        FILE_SIZE=$(stat -c%s "$OUTPUT_FILE" 2>/dev/null || stat -f%z "$OUTPUT_FILE" 2>/dev/null || echo "unknown")
        if [ "$FILE_SIZE" != "unknown" ]; then
            FILE_SIZE_KB=$((FILE_SIZE / 1024))
            echo "File size: ${FILE_SIZE_KB} KB"
        fi

        # Check if config file was copied
        CONFIG_FILE="build/tools/injector_config.ini"
        if [ -f "$CONFIG_FILE" ]; then
            echo "Configuration file: $CONFIG_FILE"
        else
            echo "Warning: Configuration file not found at expected location: $CONFIG_FILE"
        fi
    else
        echo "Warning: Output file not found at expected location: $OUTPUT_FILE"
    fi
else
    echo "Build failed with exit code: $?"
    exit 1
fi

echo "Build completed!"
echo ""
echo "Usage:"
echo "  ./build/tools/enhanced_injector.exe --help"
echo "  ./build/tools/enhanced_injector.exe"
echo ""
echo "Configuration file: build/tools/injector_config.ini"
