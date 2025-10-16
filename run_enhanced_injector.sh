#!/bin/bash
# Comprehensive script for Enhanced ReShade Injector
# This script builds, copies files, and runs the enhanced_injector

set -e

BUILD_TYPE=${1:-"RelWithDebInfo"}
NO_BUILD=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-build)
            NO_BUILD=true
            shift
            ;;
        *)
            BUILD_TYPE="$1"
            shift
            ;;
    esac
done


echo "Operating System: $(uname -s)"

# kill process enhanced_injector.exe on windows starts with MINGW64_NT-10.0
if [[ "$(uname -s)" == "MINGW64_NT"* ]]; then
    echo "Attempting to kill existing enhanced_injector.exe processes..."

    # Method 1: Try taskkill first
    if taskkill /F /IM enhanced_injector.exe 2>/dev/null; then
        echo "Successfully killed enhanced_injector.exe using taskkill"
    else
        echo "taskkill failed, trying PowerShell method..."
        # Method 2: Try PowerShell as fallback
        if powershell "Get-Process enhanced_injector -ErrorAction SilentlyContinue | Stop-Process -Force" 2>/dev/null; then
            echo "Successfully killed enhanced_injector.exe using PowerShell"
        else
            echo "No enhanced_injector.exe processes found or already terminated"
        fi
    fi
else
    echo "Not on Windows, skipping kill process enhanced_injector.exe"
    echo "Please manually kill the process enhanced_injector.exe"
    exit 1
fi


echo "Enhanced ReShade Injector - Build and Run Script"
echo "================================================="

# Step 1: Build the enhanced_injector
if [ "$NO_BUILD" = false ]; then
    echo ""
    echo "Step 1: Building Enhanced ReShade Injector..."

    # Configure and build the enhanced injector
    cmake -S . -B build -G Ninja "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" -DEXPERIMENTAL_TAB=ON
    cmake --build build --config "$BUILD_TYPE" --target enhanced_injector

    # Check if build was successful
    if [ $? -ne 0 ]; then
        echo "Build failed with exit code: $?"
        exit 1
    fi

    echo "Build completed successfully!"
else
    echo ""
    echo "Skipping build step (--no-build specified)"
fi

# Step 2: Create run directory and copy files
echo ""
echo "Step 2: Setting up run environment..."

RUN_DIR="run_tmp"
if [ ! -d "$RUN_DIR" ]; then
    mkdir -p "$RUN_DIR"
    echo "Created directory: $RUN_DIR"
fi

# Copy enhanced_injector.exe
SOURCE_EXE="build/tools/enhanced_injector.exe"
DEST_EXE="$RUN_DIR/enhanced_injector.exe"
if [ -f "$SOURCE_EXE" ]; then
    cp "$SOURCE_EXE" "$DEST_EXE"
    echo "Copied: $SOURCE_EXE -> $DEST_EXE"
else
    echo "Error: Source executable not found: $SOURCE_EXE"
    exit 1
fi

# Copy ReShade64.dll from res_injector
SOURCE_DLL="res_injector/Reshade64.dll"
DEST_DLL="$RUN_DIR/ReShade64.dll"
if [ -f "$SOURCE_DLL" ]; then
    cp "$SOURCE_DLL" "$DEST_DLL"
    echo "Copied: $SOURCE_DLL -> $DEST_DLL"
else
    echo "Warning: ReShade64.dll not found in res_injector, using default"
fi

# Step 3: Copy configuration file
echo ""
echo "Step 3: Setting up configuration..."

SOURCE_CONFIG="res_injector/injector_config.ini"
DEST_CONFIG="$RUN_DIR/injector_config.ini"

if [ -f "$SOURCE_CONFIG" ]; then
    cp "$SOURCE_CONFIG" "$DEST_CONFIG"
    echo "Copied config: $SOURCE_CONFIG -> $DEST_CONFIG"
else
    echo "Warning: Config file not found in res_injector, using default"
    # Copy default config from tools directory
    DEFAULT_CONFIG="tools/injector_config.ini"
    if [ -f "$DEFAULT_CONFIG" ]; then
        cp "$DEFAULT_CONFIG" "$DEST_CONFIG"
        echo "Copied default config: $DEFAULT_CONFIG -> $DEST_CONFIG"
    fi
fi

# Step 4: Display configuration and run
echo ""
echo "Step 4: Configuration Summary"
echo "============================="
echo "Run directory: $RUN_DIR"
echo "Executable: $DEST_EXE"
echo "ReShade DLL: $DEST_DLL"
echo "Config file: $DEST_CONFIG"

# Display config content
if [ -f "$DEST_CONFIG" ]; then
    echo ""
    echo "Configuration file contents:"
    cat "$DEST_CONFIG" | sed 's/^/  /'
fi

# Step 5: Kill existing enhanced_injector processes and run new one
echo ""
echo "Step 5: Running Enhanced ReShade Injector..."

# Kill any existing enhanced_injector processes
EXISTING_PROCESSES=$(pgrep enhanced_injector 2>/dev/null || true)
if [ -n "$EXISTING_PROCESSES" ]; then
    PROCESS_COUNT=$(echo "$EXISTING_PROCESSES" | wc -l)
    echo "Found $PROCESS_COUNT existing enhanced_injector process(es), terminating..."
    echo "$EXISTING_PROCESSES" | xargs kill -9 2>/dev/null || true
    sleep 1
    echo "Existing processes terminated."
else
    echo "No existing enhanced_injector processes found."
fi

echo "Press Ctrl+C to stop the injector"
echo "==============================================="

# Change to run directory and execute
echo "Changing to run directory: $RUN_DIR"
cd "$RUN_DIR"
./enhanced_injector.exe

echo ""
echo "Enhanced ReShade Injector stopped."
