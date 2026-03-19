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

**🐛 Latest Debug Build**: Debug build with PDB symbols for debugging and crash analysis: [Latest Debug Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug).

**🔄 Nightly Builds**: Scheduled development builds (daily): [Nightly Releases](https://github.com/pmnoxx/display-commander/releases/tag/nightly).

**🔨 Workflow artifacts**: Download from the [latest successful workflow run](https://github.com/pmnoxx/display-commander/actions/workflows/build.yml?query=branch%3Amain+is%3Asuccess) (requires GitHub account for artifact downloads).

## Features

### Display & window

- **Window mode**: Borderless windowed (aspect ratio or explicit width/height), borderless fullscreen, or no changes
- **Monitor targeting**: Choose which monitor to use; view current display info and refresh rate
- **Background black curtain**: Fill unused screen space behind a smaller game window (ADHD multi-monitor mode)
- **Alignment**: Snap window to corners or center
- **Window controls**: Minimize, restore, maximize (applied on a background thread to avoid crashes)
- **Display hotkeys**: Move to previous/next monitor (Win+Left / Win+Right), move to primary/secondary (numpad +/-), minimize/restore (Win+Down / Win+Up)
- **Prevent minimize**: Option to avoid game window minimizing on alt-tab

### Performance & FPS

- **FPS limiter**: Custom limiter with foreground and optional background caps; multiple modes (e.g. OnPresentSync, Reflex, LatentSync, Safe mode)
- **Background FPS**: Optional separate FPS limit when game is in background (checkbox + slider)
- **Live indicators**: Flip state (composed/independent), VRR status, present mode (DXGI/Vulkan/OpenGL)
- **Limit real frames**: Works with frame generation (DLSS-G, etc.) to limit real frames instead of generated ones

### Audio

- **Per-process volume**: Control game volume independently
- **Mute options**: Manual mute, mute in background, conditional background mute
- **Per-channel VU meters**: Main tab and performance overlay
- **Hotkeys**: Volume up/down, brightness up/down (configurable step)

### Latency

- **NVIDIA Reflex**: Low-latency mode integration for reduced input lag (native and configurable)
- **Inject Reflex**: Optional injection of Reflex (sleep + latency markers) for games without native Reflex
- **AntiLag II / XeLL**: AMD AntiLag 2 and XeLL support via fake NVAPI (experimental, for non-NVIDIA GPUs)
- **PCLStats (ETW)**: Optional PCLStats event reporting for Special K–style tools

### HDR & color

- **Auto color space**: Format-based color space (sRGB / scRGB / HDR10) on the swap chain (DXGI)
- **Manual color space**: Override color space (sRGB, scRGB, HDR10 ST2084/HLG) from Advanced tab
- **HDR hiding** (experimental): Option to hide HDR modes or force SDR for compatibility

### DLSS & upscaling

- **DLSS information**: Status, quality preset, scaling ratio, internal/output resolution on Main and Swapchain tabs
- **Frame Generation (FG)**: FG mode display in performance overlay when DLSS-G is active
- **DLSS overrides**: Preset overrides, internal resolution scale, M–Z presets; optional hide DLSS-G from ReShade
- **VRR status**: DXGI and NVAPI VRR state (enabled/requested/possible) on Swapchain tab

### Input & hotkeys

- **Configurable hotkeys**: Full Hotkeys tab — window/display, volume, brightness, ADHD mode, independent window toggle, etc.
- **XInput**: Controller support and hooks for remapping / compatibility
- **Windows.Gaming.Input**: Option to suppress WGI so games use XInput (helps with continue-rendering-in-background)
- **Input remapping**: Controller remapping and related presets

### Advanced & power user

- **Continue rendering in background**: Game keeps rendering when alt-tabbed (no minimize/focus spoofing)
- **Standalone / independent UI**: Run settings in a separate window or without ReShade (.NO_RESHADE, SetupDC)
- **Proxy loading**: Load as dxgi.dll, d3d9.dll, d3d11.dll, d3d12.dll, ddraw.dll, dinput8.dll, hid.dll, bcrypt.dll, version.dll, opengl32.dll, dbghelp.dll, vulkan-1.dll, winmm.dll, or winhttp.dll proxy when needed. When loaded as **winhttp.dll**, the Main tab shows a warning: the proxy is not signed by Microsoft and may cause network connection issues (blocked traffic, login failures); consider using another proxy (e.g. dxgi.dll) if you see network problems.
- **Expert – flag files**: Optional flag files change behavior (e.g. `.NO_RESHADE`, `.NODC`).
  **In the game exe directory:**
  - `.NO_RESHADE` / `.NORESHADE` — standalone mode (no ReShade; settings UI only)
  - `.NODC` — load ReShade only, do not register as addon (proxy-only)
  - `.UI` — open independent settings window at start
  - `.NO_EXIT` / `.NOEXIT` — block process exit; open independent UI when game tries to exit (debugging)
  - `.GET_PROC_ADDRESS` — GetProcAddress logging
  **In the addon (DLL) folder or `%LocalAppData%\Programs\Display_Commander\`:**
  - `.DC_CONFIG_GLOBAL` — store config and ReShade data in `%LocalAppData%\Programs\Display_Commander\Games\<game_name>\` (global per-game folder; `game_name` skips generic exe path segments like `Client` / `Binaries` / `Win64`). The empty flag file can live next to the addon **or** in the Display_Commander app data root (one place is enough).
  - `.DC_CONFIG_IN_DLL` — store config and ReShade data in the addon folder instead of the game folder
  - `.DLL_DETECTOR` — copy addon to `dlls_loaded` and exit (for detecting which DLLs the game loads)
  See [Expert: Flag files in the game directory](docs/EXPERT_FLAG_FILES.md).
- **Addon directory DLL loading**: From the same folder as the addon, **.dc64 / .dc32 / .dc / .asi** are loaded before ReShade; **.dc64r / .dc32r / .dcr** are loaded after ReShade (for addons that need the ReShade API). Post-ReShade addons use a temp copy so originals can be updated while the game runs.
- **NVIDIA Profile (Inspector)**: View and edit driver profile for the current game; apply as administrator
- **CPU control**: Core affinity and process priority (Main / Settings tab in standalone)
- **Prevent display sleep & screensaver**: Keep display awake while gaming
- **Clip cursor**: Option to clip cursor to game window
- **Image adjustment**: Brightness, gamma, saturation, hue; AutoHDR option
- **Fake NVAPI** (experimental): Spoof NVIDIA on non-NVIDIA GPUs for DLSS/Reflex-style features

## Known Issues

**⚠️ Load Order Requirement**: Display Commander must be loaded last by ReShade. This is why we've added the `zzz_` prefix to the filename - it ensures proper load order and prevents conflicts with other addons.

For a comprehensive list of known issues and workarounds, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Requirements

- Windows with **stable ReShade 6.6.2** or later
- The addon matching your game architecture: `.addon64` for 64-bit, `.addon32` for 32-bit

## Installing Display Commander as a proxy (`dxgi.dll` / `winmm.dll`) (RECOMMENDED)

**Recommended for maximum compatibility:** install Display Commander as a **DLL proxy** in the **game exe folder**—start with **`winmm.dll`** or **`dxgi.dll`**; if the game does not load those from its directory, try **another supported proxy name** (e.g. `d3d11.dll`, `d3d12.dll`, `version.dll`; full list under **Features → Proxy loading** above). Copy `zzz_display_commander.addon64` (or `.addon32` for 32-bit) there and **rename** it to that proxy filename. Put ReShade beside it as **`ReShade64.dll`** (64-bit) or **`ReShade32.dll`** (32-bit); Display Commander loads ReShade from that DLL. This layout avoids depending on a global ReShade install and works with titles that only load DLLs from the game folder.

**Common proxy names**

| Name | Typical use |
|------|-------------|
| **dxgi.dll** | DirectX 10/11/12 titles that load DXGI from the game directory. |
| **winmm.dll** | Very early load (before DXGI in some setups); use if `dxgi.dll` is not loaded from the game folder or you need this load order. |

Do not leave **two** different proxies in the same folder if both would wrap the same API (e.g. conflicting `dxgi.dll` replacements). Pick one approach per game.

**Steps (64-bit game)**

1. Copy `zzz_display_commander.addon64` into the **same directory as the game executable**.
2. Rename that file to `dxgi.dll` **or** `winmm.dll` (only one proxy name as above).
3. Add **ReShade** next to it:
   - Download the official addon installer: [ReShade_Setup_6.7.3_Addon.exe](https://reshade.me/downloads/ReShade_Setup_6.7.3_Addon.exe).
   - Open the `.exe` with **7-Zip** (right-click → *7-Zip* → *Open archive*) and extract **`ReShade64.dll`** into the **game exe folder** (same place as the renamed Display Commander proxy).
4. Launch the game; open the ReShade overlay and enable Display Commander on the Add-ons tab if needed.

**32-bit games**: Use `zzz_display_commander.addon32` renamed to the same proxy pattern your title expects, and extract **`ReShade32.dll`** from the same installer archive instead of `ReShade64.dll`.

ReShade **6.6.2 or later** is required; the 6.7.3 addon package above meets that. Newer ReShade addon installers from [reshade.me](https://reshade.me) work the same way if you prefer a newer `ReShade64.dll` / `ReShade32.dll`.

**Config and ReShade paths** (proxy installs too): Same rules as in the **Config and ReShade paths** paragraph in the next section.

## Installing Display Commander as a ReShade addon (may cause issue; use as backup)

Use this path when **ReShade** is installed with the official setup from **[reshade.me](https://reshade.me)** and loads as **`dxgi.dll`**, **`d3d9.dll`**, **`opengl32.dll`**, or the **Vulkan layer**—then copy Display Commander’s `.addon64` / `.addon32` into the same place ReShade uses for that game (or your global ReShade folder). If that setup misbehaves, switch to the **proxy + `ReShade64.dll`** / **`ReShade32.dll`** method in the section above.

**Prerequisites**: You must have **stable ReShade 6.6.2** or later installed.

1. Download a prebuilt addon from Releases (CI uploads artifacts for both x64 and x86), or build from source.
2. Copy the file `zzz_display_commander.addon64` (or `.addon32` for 32-bit) to the folder where ReShade is loaded for your game (the same folder as the ReShade runtime, e.g., `dxgi.dll`).
   - Alternatively, place it into your global ReShade installation directory (for example `D:\\Program Files\\ReShade`).
3. Launch the game, open the ReShade overlay (Home by default), go to the Add-ons tab, and locate "Display Commander".

**Config and ReShade paths**: By default, config (e.g. `DisplayCommander.toml`, `ReShade.ini`) is stored in the **game exe directory**. Create an empty **`.DC_CONFIG_GLOBAL`** in the **addon folder** or in **`%LocalAppData%\Programs\Display_Commander\`** to use `%LocalAppData%\Programs\Display_Commander\Games\<game_name>\` (game name = first path segment above the exe that is not a generic engine folder—`Client`, `Binaries`, `Win64`, `Win32`, `Bin`, `x64`, `x86`—otherwise the exe name without `.exe`, e.g. `Wuthering Waves` or `Sekiro`). **`.DC_CONFIG_IN_DLL`** in the addon folder stores config next to the addon. Priority: `.DC_CONFIG_GLOBAL` (either location) beats `.DC_CONFIG_IN_DLL`.

**Note**: For the latest stable release compatible with ReShade 6.6.2, download from [Latest Release](https://github.com/pmnoxx/display-commander/releases).

### Installing OptiScaler

[OptiScaler](https://github.com/optiscaler/OptiScaler) enables DLSS/FSR-style upscaling on AMD and Intel GPUs (and can be used alongside Display Commander). To install it in a game folder that uses Display Commander’s **dlls_to_load** layout:

1. **Download** the OptiScaler archive from the [OptiScaler releases](https://github.com/optiscaler/OptiScaler/releases) (e.g. [v0.7.9](https://github.com/optiscaler/OptiScaler/releases/tag/v0.7.9) — file `OptiScaler_0.7.9.7z`).
2. **Unpack** the contents into your game’s **`dlls_to_load/before_reshade`** folder (DLLs here load before ReShade). You can also use **`dlls_to_load`** (root) for the same “before ReShade” phase; **`dlls_to_load/after_reshade`** is for addons that must load after ReShade.
3. **Run** `setup_windows.bat` from that folder:
   - When prompted, choose **winmm.dll**.
   - Choose **Nvidia** (or the option that matches your use case).

OptiScaler has no official website; use only the [GitHub repo](https://github.com/optiscaler/OptiScaler), their Discord, or Nitec’s NexusMods page. It is free; any site asking for payment is a scam.

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
git clone --recurse-submodules https://github.com/pmnoxx/display-commander.git
cd display-commander
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
- XInput, Windows Multimedia (winmm), and DbgHelp libraries are loaded dynamically to avoid error code 126 (module not found) on systems where these libraries are not available. DbgHelp is supported both as a runtime dependency (stack traces when available) and as a proxy: the addon can be loaded as dbghelp.dll, forwarding DbgHelp API calls to the system dbghelp.dll. Similarly, when loaded as hid.dll or bcrypt.dll, the addon forwards HID and CNG (BCrypt) API calls to the system DLLs.

## Continuous Integration

GitHub Actions builds x64 and x86 on pushes and PRs and uploads the resulting `.addon64` and `.addon32` as artifacts. Tag pushes also create releases.

- **Latest Build**: Every successful push to `main` updates the [Latest Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_build) release (bleeding edge).
- **Latest Debug Build**: Every successful push to `main` also updates the [Latest Debug Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug) release (debug binaries with PDB symbols).
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
- **Swapchain HDR Upgrade** (scRGB / HDR10): Approach inspired by [AutoHDR-ReShade](https://github.com/EndlesslyFlowering/AutoHDR-ReShade) (EndlesslyFlowering)

See `CHANGELOG.md` for version history.


---

<p align="center">
  <a href="https://ko-fi.com/pmnox" target="_blank">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support on Ko‑fi" />
  </a>
  <br/>
  <a href="https://ko-fi.com/pmnox">ko-fi.com/pmnox</a>

</p>
