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
- **Black curtain**: Optional full-screen black layers on the game monitor and/or other monitors (windowed games, multi-monitor)
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

- **HDR hiding** (experimental): Option to hide HDR modes or force SDR for compatibility

### DLSS & upscaling

- **DLSS information**: Status, quality preset, scaling ratio, internal/output resolution on Main and Swapchain tabs
- **Frame Generation (FG)**: FG mode display in performance overlay when DLSS-G is active
- **DLSS overrides**: Preset overrides, internal resolution scale, M–Z presets; optional hide DLSS-G from ReShade
- **VRR status**: DXGI and NVAPI VRR state (enabled/requested/possible) on Swapchain tab

### Input & hotkeys

- **Configurable hotkeys**: Full Hotkeys tab — window/display, volume, brightness, black curtain (other displays), independent window toggle, etc.
- **XInput**: Controller support and hooks for remapping / compatibility
- **Windows.Gaming.Input**: Option to suppress WGI so games use XInput (helps with continue-rendering-in-background)
- **Input remapping**: Controller remapping and related presets

### Advanced & power user

- **Continue rendering in background**: Game keeps rendering when alt-tabbed (no minimize/focus spoofing)
- **Standalone / independent UI**: Run settings in a separate window or without ReShade (.NO_RESHADE, SetupDC)
- **Proxy loading**: Rename the built addon (`zzz_display_commander.addon64` / `.addon32`) next to the game to one of these proxy DLL names: **dxgi.dll**, **d3d9.dll**, **d3d11.dll**, **d3d12.dll**, **ddraw.dll**, **hid.dll**, **version.dll**, **opengl32.dll**, **dbghelp.dll**, **vulkan-1.dll**, or **winmm.dll**.
  **In the addon (DLL) folder or `%LocalAppData%\Programs\Display_Commander\`:**
  - `.DC_CONFIG_GLOBAL` — store config and ReShade data in `%LocalAppData%\Programs\Display_Commander\Games\<game_name>\` (global per-game folder; `game_name` skips generic exe path segments like `Client` / `Binaries` / `Win64`). The empty flag file can live next to the addon **or** in the Display_Commander app data root (one place is enough).
  - `.DC_CONFIG_IN_DLL` — store config and ReShade data in the addon folder instead of the game folder
  See [Expert: Flag files in the game directory](docs/EXPERT_FLAG_FILES.md).
- **Addon directory DLL loading**: From the same folder as the addon, **.dc64 / .dc32 / .dc / .asi** are loaded before ReShade; **.dc64r / .dc32r / .dcr** are loaded after ReShade (for addons that need the ReShade API). All of these DLLs are loaded in place; Windows keeps them locked while the game is running, so you may need to exit the game before overwriting them with newer builds.
- **NVIDIA Profile (Inspector)**: View and edit driver profile for the current game; apply as administrator
- **CPU control**: Core affinity and process priority (Main / Settings tab in standalone)
- **Clip cursor**: Option to clip cursor to game window

## Continuous Integration

GitHub Actions builds x64 and x86 on pushes and PRs and uploads the resulting `.addon64` and `.addon32` as artifacts. Tag pushes also create releases.

- **Latest Build**: Every successful push to `main` updates the [Latest Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_build) release (bleeding edge).
- **Latest Debug Build**: Every successful push to `main` also updates the [Latest Debug Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug) release (debug binaries with PDB symbols).
- **Nightly**: A scheduled build runs daily at 2:00 AM UTC and updates the [Nightly](https://github.com/pmnoxx/display-commander/releases/tag/nightly) release.

## Support

Need help? Check out our [Support Guide](SUPPORT.md) for detailed information on getting assistance.

**Quick Support Links:**
- **RenoDX Discord**: [Join our community](https://discord.com/invite/jz6ujVpgFB)
- **Support Thread**: [Display Commander Support](https://discord.com/channels/1408098019194310818/1486668882776293558)
- **GitHub Issues**: [Report bugs or request features](https://github.com/pmnoxx/display-commander/issues)

The RenoDX Discord community is the best place to get real-time help, discuss features, and connect with other users.

## License

This project is distributed under the terms in [LICENSE](LICENSE). Third-party components and their notices are listed in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Credits

- ReShade and its addon SDK
- Dear ImGui (via ReShade dependencies)
- NVIDIA NVAPI headers/libs (`external/nvapi`)
- Additional third-party code under `external/` — see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)
- **VBlank Scanline Sync**: Based on the algorithm explained by Kaldaien (Special-K creator)

See `CHANGELOG.md` for version history.

---

<p align="center">
  <a href="https://ko-fi.com/pmnox" target="_blank">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support on Ko‑fi" />
  </a>
  <br/>
  <a href="https://ko-fi.com/pmnox">ko-fi.com/pmnox</a>

</p>
