#!/bin/bash

# Build and run Game Commander
echo "Building Game Commander..."

# Create build directory
mkdir -p build

# Configure and build
cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
ninja game_commander

if [ $? -eq 0 ]; then
    echo "Build successful! Running Game Commander..."
    cd ..
    ./build/tools/game_commander/game_commander
else
    echo "Build failed!"
    cd ..
    exit 1
fi
