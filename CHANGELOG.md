## v0.10.0 (2026-01-XX)

- **Performance monitoring** - Added experimental Performance tab with detailed timing measurements for overlay draw, present handlers, and frame statistics
- **PresentMon integration** - Added PresentMon thread support with comprehensive debug information and frame analysis
- **System volume control** - Added system volume slider and hotkeys for volume up/down control
- **CPU management** - Added UI to set CPU affinity (number of cores used) and process priority adjustment
- **Native frame pacing** - Added native frame pacing feature for frame generation to all FPS limiters
- **DLL loading enhancements** - Added "Load DLL Early" option, DllsToLoadBefore, and DllLoadingDelayMs settings for better compatibility
- **Window management improvements** - Added SuppressWindowChanges config setting to fix black screen issues in games with DPI scaling
- **VRR debugging** - Added VRR debug mode to performance overlay for variable refresh rate monitoring
- **Hotkey expansion** - Added support for F13-F24 hotkeys, system volume controls, and gamepad shortcuts
- **ReShade 6.6.2 support** - Updated requirements to ReShade 6.6.2 with improved compatibility
- **Hooking improvements** - Enhanced hooking to d3d11.dll, d3d12.dll, and dxgi.dll with better ReShade integration
- **XInput enhancements** - Improved XInput support for multiple libraries and added XInputGetCapabilities hooks
- **Reflex improvements** - Fixed reflex boost mode, improved reflex sleep suppression, and better integration with frame limiters
- **Memory leak fixes** - Fixed multiple memory leaks in DXGI swapchain hooks and reference counting
- **Stability fixes** - Fixed crashes in WuWa, DNA games, Yakuza 4 Remastered, and various other games
- **Swapchain improvements** - Fixed reference counting issues, improved GetIndependentFlipState measurement, and better swapchain wrapper handling
- **UI enhancements** - Improved hotkeys UI, added gamepad chord support, and various UI cleanup and organization improvements
- **Streamline integration** - Improved Streamline upgrade interface integration and NGX parameter adjustment API

## v0.9.0 (2025-10-18)

- **FPS limiter enhancements** - Added fps limiter for DLSS-g through native reflex
- **Reflex integration** - Added "reflex" fps limiter mode, which can either control native reflex or inject reflex
- **HDR compatibility fixes** - Added a fix for HDR in NVAPI HDR games (Sekiro, Resident Evil, Hitman, etc.)
- **DLSS Profile controls** - Added controls for overriding DLSS Profile A/B/C/D/E/...
- **Stability improvements** - Fixed various stability issues
- **DX9 support** - Added DX9 support
- **ReShade compatibility** - Added support for stable ReShade 6.5.1/6.6.0
- **UE4/UE5 compatibility** - Fixed compatibility issues with UE4/UE5 UUU

## v0.3.1 (2025-08-26)

- **Tearing control improvements** - Removed "Allow Tearing" feature to simplify VSync controls
- **Reflex enhancements** - Upgraded 95% button to 96% to match Reflex standards and renamed to 'Reflex Cap' button
- **Latency reduction** - Added experimental feature to delay CPU sim thread by fixed amount to reduce latency
- **Input shortcuts** - Added experimental Ctrl+M mute/unmute shortcut for quick audio control
- **Backbuffer customization** - Added experimental feature to override backbuffer size
- **UI improvements** - Fixed 96% button color and added warning about restarting games after changing v-sync/tearing options
- **Performance optimizations** - Used shader_ptr instead of spinlock and fixed race conditions related to g_window_state
- **Code quality** - Extensive code cleanup and removal of sync interval setting

## v0.3.0 (2025-08-24)

- **Complete UI modernization** - Migrated from legacy settings-based UI to modern ImGui-based interface
- **New tabbed interface** - Reorganized UI into logical tabs: Main, Device Info, Window Info, Swapchain, and Developer
- **Enhanced developer features** - Complete rewrite of developer tab with improved NVAPI status display and HDR controls
- **Improved settings management** - New settings wrapper system for better organization and persistence
- **Better user experience** - More responsive UI with immediate feedback and better visual organization
- **Enhanced monitor management** - Improved multi-monitor support with better resolution and refresh rate handling
- **Developer tool improvements** - Better NVAPI integration, HDR10 colorspace fixes, and comprehensive debugging tools
- **Code architecture improvements** - Streamlined addon structure with better separation of concerns
- **Performance optimizations** - Improved caching and reduced unnecessary operations for better responsiveness

## v0.2.4 (2025-08-23)

- **Improved compatibility** - Dropped custom DLL requirement, making addon more accessible to all users
- **New developer toggle** - Added "Enable unstable ReShade features" checkbox for advanced users who need custom dxgi.dll
- **Input control gating** - Input blocking features now require explicit opt-in via developer toggle for safety
- **Better user experience** - Clear separation between stable and experimental features
- **Enhanced safety** - Prevents accidental use of potentially unstable features by default

## v0.2.3 (2025-08-23)

- **V-Sync 2x-4x implementation** - Implemented v-sync 2x, 3x, 4x functionality in supported games
- **Swapchain improvements** - Enhanced swapchain event handling for better v-sync compatibility
- **Window management updates** - Improved window management system for v-sync modes
- **Code optimization** - Streamlined addon structure and removed unnecessary code

## v0.2.2 (2025-08-23)

- **Critical bug fix** - Fixed sync interval crashes by preventing invalid Present calls on flip-model swap chains
- **DXGI swap effect detection** - Added detection of swap effect type to avoid invalid sync intervals
- **Multi-vblank safety** - Clamp multi-vblank (2-4) to 1 on flip-model chains to prevent crashes
- **V-Sync 2x-4x compatibility** - Only allow 2-4 v-blanks on bitblt swap effects (SEQUENTIAL/DISCARD)
- **Stability improvements** - Removed present_mode override that could cause compatibility issues
- **Crash prevention** - Fixes games crashing with sync_value == 3 (V-Sync 2x)

## v0.2.1 (2025-08-23)

- **Documentation improvements** - Added direct download links to latest builds in README for easier access
- **Build accessibility** - Users can now directly download the latest x64 and x86 builds from the README

## v0.2.0 (2025-08-23)

- **Complete UI rewrite** - New modern interface with improved tabs and better organization
- **Per-display persistent settings** - Support for up to 4 displays with individual resolution and refresh rate settings
- **Sync interval feature** - Added V-sync 2x-4x modes support for enhanced synchronization
- **Input blocking** - Implemented background input blocking functionality
- **32-bit build support** - Added support for 32-bit builds with dedicated build script
- **Build system improvements** - Removed renodx- prefix from build outputs, improved build scripts and CI
- **Enhanced monitor management** - Better multi-monitor support with per-display settings persistence
- **Bug fixes** - Fixed v-sync 2x-4x modes and various build issues
- **Documentation updates** - Added comprehensive known bugs section and improved README

## v0.1.1 (2025-08-23)

- Added FPS counter with Min FPS and 1% Low metrics to Important Information section
- Enhanced performance monitoring with real-time FPS statistics

## v0.1.0 (2025-08-23)

- Initial release of Display Commander addon
- Added Audio settings (volume, manual mute, background mute options)
- Unmute-on-start when "Audio Mute" is unchecked
- Custom FPS limiter integration and background limit
- Basic window mode/resolution controls and monitor targeting
- NVIDIA Reflex toggle and status display
