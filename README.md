# ReShade Display Commander

ReShade Display Commander is a ReShade addon that provides in-game control over display, windowing, performance, and audio. It adds a simple UI inside the ReShade overlay to adjust borderless/fullscreen behavior, window size and alignment, monitor targeting, FPS limiting (including background caps), NVIDIA Reflex, and per-process audio volume/mute.

Note: Applying window operations from the main thread can crash some apps. This addon performs them on a background thread.

## Features

- Window management: Borderless windowed (aspect ratio or explicit width/height) and borderless fullscreen
- Monitor targeting: Choose which monitor to use and view current display info
- Background black curtain: Fill unused screen space behind a smaller game window
- Alignment options: Snap window to corners or center
- Auto-apply (continuous monitoring): Keep size/position in sync
- FPS limiter: Foreground and background caps using a custom limiter
- Audio controls: Per-process volume, manual mute, mute in background, conditional background mute
- NVIDIA Reflex: Simple on/off toggle with status
- Live indicators: PCL AV latency (30-frame average), Reflex status, and flip state

## Known Bugs

- **NVAPI support is broken at the moment** - NVIDIA-specific features may not work properly
- **Sync interval 2x-4x (vsync) is broken in ReShade** - V-sync functionality may not work as expected
- **Multiple monitors with different resolutions/refresh rates aren't supported** - The addon may not handle multi-monitor setups with varying display specifications correctly

## Requirements

- Windows with ReShade (addon support enabled)
- The addon matching your game architecture: `.addon64` for 64-bit, `.addon32` for 32-bit

## Installation

1. Download a prebuilt addon from Releases (CI uploads artifacts for both x64 and x86), or build from source.
2. Copy the file `display_commander.addon64` (or `.addon32` for 32-bit) to the folder where ReShade is loaded for your game (the same folder as the ReShade runtime, e.g., `dxgi.dll`).
   - Alternatively, place it into your global ReShade installation directory (for example `D:\\Program Files\\ReShade`).
3. Launch the game, open the ReShade overlay (Home by default), go to the Add-ons tab, and locate "Display Commander".

## Usage

Inside the ReShade overlay, Display Commander exposes:

- Display Settings: Window mode, width/height or aspect ratio, target monitor, background curtain, alignment, and Auto-apply
- Monitor & Display: Dynamic monitor settings and current display info
- Audio: Volume, manual mute, mute in background, and conditional background mute
- Window Controls: Minimize, restore, maximize (applied from a background thread)
- Reflex: Enable/disable NVIDIA Reflex
- Important Info: PCL AV latency, Reflex status, and flip state

## Build from source

Prerequisites:

- Git
- CMake 3.20+
- Ninja
- MSVC toolchain (Visual Studio 2022 or Build Tools)

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/yourname/reshade-display-commander.git
cd reshade-display-commander
```

Build (x64):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
# Output: build/display_commander.addon64
```

Build (x86, 32-bit):

```bash
cmake -S . -B build32 -G "Ninja Multi-Config" -A Win32
cmake --build build32 --config Release --parallel
# Output: build32/Release/display_commander.addon32 (or build32/display_commander.addon32)
```

Notes:

- This project requires the Ninja generator. If another generator is used, configuration will fail.
- Initialize submodules before building: `git submodule update --init --recursive`.
- NVAPI features are linked if `external/SpecialK/depends/lib/nvapi` is present. Missing NVAPI libs will only disable those features.

## Continuous Integration

GitHub Actions builds x64 and x86 on pushes and PRs and uploads the resulting `.addon64` and `.addon32` as artifacts. Tag pushes also create releases.

## Troubleshooting

- "This project requires the Ninja generator": Configure with `-G Ninja` (or `"Ninja Multi-Config"` for the 32-bit example above).
- "Missing submodule: external/reshade": Run `git submodule update --init --recursive`.
- "NVAPI libs not found ...": Optional; only NVAPI-based features will be unavailable.
- "No addon files found" after build: Ensure Release config and correct architecture; check `build/` or `build32/Release/` for the expected output name.

## Credits

- ReShade and its addon SDK
- Dear ImGui (via ReShade dependencies)
- Special K project (NVAPI headers/libs in `external/SpecialK`)
- Additional third-party code under `external/` (stb, fpng, etc.)

See `CHANGELOG.md` for version history.

