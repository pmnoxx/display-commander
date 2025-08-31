#!/bin/bash

# Non-interactive, build 32-bit (.addon32) using MSVC Win32

set -euo pipefail

# Ensure submodules are initialized
git submodule update --init --recursive

BUILD_DIR="build32"

# Configure for 32-bit using Visual Studio 2022 generator
cmake -S . -B "$BUILD_DIR" -G "Visual Studio 17 2022" -A Win32 -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build "$BUILD_DIR" --config Release --parallel

# Verify artifact(s)
FOUND=0
while IFS= read -r f; do
  if [ -n "$f" ]; then
    echo "Built artifact: $f"
    FOUND=1
  fi
done < <(find "$BUILD_DIR" -type f -name "zzz_display_commander.addon32" 2>/dev/null || true)

if [ "$FOUND" -eq 0 ]; then
  echo "No zzz_display_commander.addon32 artifact found in $BUILD_DIR" >&2
  exit 1
fi


