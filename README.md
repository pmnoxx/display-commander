# ReShade Display Commander

ReShade Display Commander is a ReShade addon that provides in-game control over display, windowing, performance, and audio. It adds a simple UI inside the ReShade overlay to adjust borderless/fullscreen behavior, window size and alignment, monitor targeting, FPS limiting (including background caps), NVIDIA Reflex, and per-process audio volume/mute.

Note: Applying window operations from the main thread can crash some apps. This addon performs them on a background thread.

-Latest builds:
-- x64: [display_commander.addon64](../../releases/latest/download/display_commander.addon64)      
-- x86: [display_commander.addon32](../../releases/latest/download/display_commander.addon32)    

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
- ...

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
- NVAPI features are linked if NVIDIA's NVAPI is present under `external/nvapi` (headers at `external/nvapi`, libs at `external/nvapi/{x86,amd64}`). Missing NVAPI libs will only disable those features.

## Continuous Integration

GitHub Actions builds x64 and x86 on pushes and PRs and uploads the resulting `.addon64` and `.addon32` as artifacts. Tag pushes also create releases.

## Troubleshooting

- "This project requires the Ninja generator": Configure with `-G Ninja` (or `"Ninja Multi-Config"` for the 32-bit example above).
- "Missing submodule: external/reshade": Run `git submodule update --init --recursive`.
- "NVAPI libs not found ...": Optional; only NVAPI-based features will be unavailable.
- "No addon files found" after build: Ensure Release config and correct architecture; check `build/` or `build32/Release/` for the expected output name.

## Support

Need help? Check out our [Support Guide](SUPPORT.md) for detailed information on getting assistance.

**Quick Support Links:**
- **HDR Den Discord**: [Join our community](https://discord.com/invite/WJ9YZctPND)
- **Support Thread**: [Display Commander Support](https://discord.com/channels/1161035767917850784/1403983735031857162)
- **GitHub Issues**: [Report bugs or request features](https://github.com/pmnoxx/reshade-display-commander/issues)

The HDR Den Discord community is the best place to get real-time help, discuss features, and connect with other users.

## Credits

- ReShade and its addon SDK
- Dear ImGui (via ReShade dependencies)
- NVIDIA NVAPI headers/libs (`external/nvapi`)
- Additional third-party code under `external/` (stb, fpng, etc.)

See `CHANGELOG.md` for version history.


---

<p align="center">
  <a href="https://ko-fi.com/pmnox" target="_blank">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support on Ko‑fi" />
  </a>
  <br/>
  <a href="https://ko-fi.com/pmnox">ko-fi.com/pmnox</a>
  
</p>
