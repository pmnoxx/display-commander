#!/bin/bash

# Ensure submodules are initialized
git submodule update --init --recursive

# Build the project for 32-bit using Ninja Multi-Config and Win32 platform
cmake -S . -B build32 -G "Ninja Multi-Config" -A Win32
cmake --build build32 --config Release

OUT32_MCFG="build32/Release/renodx-display_commander.addon32"
OUT32_NINJA="build32/renodx-display_commander.addon32"

if [ -f "$OUT32_MCFG" ]; then
  echo "32-bit artifact found in build32/Release directory"
elif [ -f "$OUT32_NINJA" ]; then
  echo "32-bit artifact found in build32 directory"
else
  echo "No 32-bit artifact found in build32 directory"
  exit 1
fi

#!/bin/bash

# Ensure submodules are initialized
git submodule update --init --recursive

# Configure and build 32-bit with Ninja Multi-Config
cmake -S . -B build32 -G "Ninja Multi-Config" -A Win32
cmake --build build32 --config Release

# Expected output name from CMake properties: PREFIX "renodx-" + target + SUFFIX ".addon32"
OUT32="build32/Release/renodx-display_commander.addon32"
OUT32_NINJA="build32/renodx-display_commander.addon32"
OUTDLL="build32/Release/display_commander.dll"
OUTDLL_NINJA="build32/display_commander.dll"
OUT64="build32/Release/renodx-display_commander.addon64"
OUT64_NINJA="build32/renodx-display_commander.addon64"

if [ -f "$OUT32" ]; then
  cp "$OUT32" "D:/Program Files/ReShade/renodx-display_commander.addon32"
elif [ -f "$OUT32_NINJA" ]; then
  cp "$OUT32_NINJA" "D:/Program Files/ReShade/renodx-display_commander.addon32"
elif [ -f "$OUT64" ] || [ -f "$OUT64_NINJA" ]; then
  echo "Error: Built 64-bit artifact. Ensure -A Win32 is used or clean build32 dir."
  exit 1
elif [ -f "$OUTDLL" ]; then
  cp "$OUTDLL" "D:/Program Files/ReShade/display_commander.dll"
elif [ -f "$OUTDLL_NINJA" ]; then
  cp "$OUTDLL_NINJA" "D:/Program Files/ReShade/display_commander.dll"
else
  echo "No known output artifact found in build32 directory"
  exit 1
fi


