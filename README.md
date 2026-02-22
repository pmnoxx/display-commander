# ReShade Display Commander

ReShade Display Commander is a ReShade addon that provides in-game control over display, windowing, performance, and audio. It adds a simple UI inside the ReShade overlay to adjust borderless/fullscreen behavior, window size and alignment, monitor targeting, FPS limiting (including background caps), and per-process audio volume/mute.

Note: Applying window operations from the main thread can crash some apps. This addon performs them on a background thread.

**✅ Version Requirement**: This addon requires **stable ReShade 6.6.2** or later. The addon is now fully compatible with stable ReShade releases.

**Latest stable release**: [Latest Release](https://github.com/pmnoxx/display-commander/releases) - Compatible with ReShade 6.6.2

## 📥 Latest Builds

| Architecture | Download |
|-------------|----------|
| **x64 (64-bit)** | [zzz_display_commander.addon64](https://github.com/pmnoxx/display-commander/releases/latest/download/zzz_display_commander.addon64) |
| **x86 (32-bit)** | [zzz_display_commander.addon32](https://github.com/pmnoxx/display-commander/releases/latest/download/zzz_display_commander.addon32) |

**🔄 Latest Build (bleeding edge)**: Build from the latest main branch, updated on every push: [Latest Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_build).

**🔄 Nightly Builds**: Scheduled development builds (daily): [Nightly Releases](https://github.com/pmnoxx/display-commander/releases/tag/nightly).

**🔨 Workflow artifacts**: Download from the [latest successful workflow run](https://github.com/pmnoxx/display-commander/actions/workflows/build.yml?query=branch%3Amain+is%3Asuccess) (requires GitHub account for artifact downloads).

## Features

- Window management: Borderless windowed (aspect ratio or explicit width/height) and borderless fullscreen
- Monitor targeting: Choose which monitor to use and view current display info
- Background black curtain: Fill unused screen space behind a smaller game window
- Alignment options: Snap window to corners or center
- Auto-apply (continuous monitoring): Keep size/position in sync
- FPS limiter: Foreground and background caps using a custom limiter
- Audio controls: Per-process volume, manual mute, mute in background, conditional background mute
- Live indicators: flip state
- **NVIDIA Reflex support**: Low-latency mode integration for reduced input lag
- **AntiLag II/XeLL support**: AMD AntiLag 2 and XeLL latency reduction technologies (via fake NVAPI)

## Known Issues

**⚠️ Load Order Requirement**: Display Commander must be loaded last by ReShade. This is why we've added the `zzz_` prefix to the filename - it ensures proper load order and prevents conflicts with other addons.

## Known Issues

For a comprehensive list of known issues and workarounds, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Requirements

- Windows with **stable ReShade 6.6.2** or later
- The addon matching your game architecture: `.addon64` for 64-bit, `.addon32` for 32-bit

## Installation

**Prerequisites**: You must have **stable ReShade 6.6.2** or later installed.

1. Download a prebuilt addon from Releases (CI uploads artifacts for both x64 and x86), or build from source.
2. Copy the file `zzz_display_commander.addon64` (or `.addon32` for 32-bit) to the folder where ReShade is loaded for your game (the same folder as the ReShade runtime, e.g., `dxgi.dll`).
   - Alternatively, place it into your global ReShade installation directory (for example `D:\\Program Files\\ReShade`).
3. Launch the game, open the ReShade overlay (Home by default), go to the Add-ons tab, and locate "Display Commander".

**Note**: For the latest stable release compatible with ReShade 6.6.2, download from [Latest Release](https://github.com/pmnoxx/display-commander/releases).

## Usage

Inside the ReShade overlay, Display Commander exposes:

- Display Settings: Window mode, width/height or aspect ratio, target monitor, background curtain, alignment, and Auto-apply
- Monitor & Display: Dynamic monitor settings and current display info
- Audio: Volume, manual mute, mute in background, and conditional background mute
- Window Controls: Minimize, restore, maximize (applied from a background thread)
- Important Info: flip state

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
# Output: build/zzz_display_commander.addon64
```

Build (x86, 32-bit):

```bash
cmake -S . -B build32 -G "Ninja Multi-Config" -A Win32
cmake --build build32 --config Release --parallel
# Output: build32/Release/zzz_display_commander.addon32 (or build32/zzz_display_commander.addon32)
```

Notes:

- This project requires the Ninja generator. If another generator is used, configuration will fail.
- Initialize submodules before building: `git submodule update --init --recursive`.
- NVAPI features are statically linked if NVIDIA's NVAPI is present under `external/nvapi` (headers at `external/nvapi`, static libs at `external/nvapi/{x86,amd64}`). Missing NVAPI libs will only disable those features. Static linking is used by default to avoid DLL dependency issues.
- XInput, Windows Multimedia (winmm), and DbgHelp libraries are loaded dynamically to avoid error code 126 (module not found) on systems where these libraries are not available.

## Continuous Integration

GitHub Actions builds x64 and x86 on pushes and PRs and uploads the resulting `.addon64` and `.addon32` as artifacts. Tag pushes also create releases.

- **Latest Build**: Every successful push to `main` updates the [Latest Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_build) release (bleeding edge).
- **Nightly**: A scheduled build runs daily at 2:00 AM UTC and updates the [Nightly](https://github.com/pmnoxx/display-commander/releases/tag/nightly) release.

## Troubleshooting

- **"Addon not loading" or "Compatibility issues"**: Ensure you're using stable ReShade 6.6.2 or later. Download the latest release from [Latest Release](https://github.com/pmnoxx/display-commander/releases).
- "This project requires the Ninja generator": Configure with `-G Ninja` (or `"Ninja Multi-Config"` for the 32-bit example above).
- "Missing submodule: external/reshade": Run `git submodule update --init --recursive`.
- "NVAPI libs not found ...": Optional; only NVAPI-based features will be unavailable.
- "No addon files found" after build: Ensure Release config and correct architecture; check `build/` or `build32/Release/` for the expected output name (should be `zzz_display_commander.addon64` or `zzz_display_commander.addon32`).

## Feature Proposals

Have ideas for new features? Check out our [Feature Proposals](FEATURE_PROPOSALS.md) to see what's being considered for future development.

## Support

Need help? Check out our [Support Guide](SUPPORT.md) for detailed information on getting assistance.

**Quick Support Links:**
- **RenoDX Discord**: [Join our community](https://discord.com/invite/jz6ujVpgFB)
- **Support Thread**: [Display Commander Support](https://discord.com/channels/1408098019194310818/1423918603035476041)
- **GitHub Issues**: [Report bugs or request features](https://github.com/pmnoxx/display-commander/issues)

The RenoDX Discord community is the best place to get real-time help, discuss features, and connect with other users.

## Credits

- ReShade and its addon SDK
- Dear ImGui (via ReShade dependencies)
- NVIDIA NVAPI headers/libs (`external/nvapi`)
- Additional third-party code under `external/` (stb, fpng, etc.)
- **VBlank Scanline Sync**: Based on the algorithm used in Special-K by Kaldaien

See `CHANGELOG.md` for version history.


---

<p align="center">
  <a href="https://ko-fi.com/pmnox" target="_blank">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support on Ko‑fi" />
  </a>
  <br/>
  <a href="https://ko-fi.com/pmnox">ko-fi.com/pmnox</a>

</p>
