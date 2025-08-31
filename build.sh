#!/bin/bash

# Ensure submodules are initialized
# git submodule update --init --recursive

# Build the project
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

OUT64_NINJA="build/zzz_display_commander.addon64"

if [ -f "$OUT64_NINJA" ]; then
  echo "64-bit artifact found in build directory"
else
  echo "No 64-bit artifact found in build directory"
  exit 1
fi
