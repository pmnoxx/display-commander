**When releasing:** the version is stored in one place only. Update `src/addons/display_commander/CMakeLists.txt` (`DISPLAY_COMMANDER_VERSION_MAJOR`/`MINOR`/`PATCH`). CMake passes these into the build; `version.hpp` uses them and derives the version string. Do not edit `version.hpp` for version numbers. See `VERSION_BUMPING.md` for the bump script.

---
## v0.12.20 (2026-02-21)

- **ADHD Multi-Monitor Mode** - Now reacts when the game window is moved to another display: the black background overlay is repositioned so it covers all monitors except the one the game is on.

## v0.12.19 (2026-02-21)

- **Prevent Minimize** - Default on (Advanced tab). Prevents game window minimize on alt-tab (e.g. Doom Dark Ages).

## v0.12.18 (2026-02-21)

- **Prevent Minimize** - Default changed from off to on (Advanced tab). When enabled, the game window is not minimized when it loses focus (e.g. alt-tab).

## v0.12.17 (2026-02-21)

- **Continue rendering in background** - When enabled, the game no longer sees that it is minimized (e.g. after alt-tab), so it keeps rendering as if it were in the foreground.

## v0.12.16 (2026-02-21)

- **Win+Left / Win+Right** - Fix target display on game load: when loading from config, validate target/selected display IDs against current monitors; if stored ID is empty or stale (e.g. after reboot), set from game window and sync both so Win+Left/Win+Right works reliably.

## v0.12.15 (2026-02-21)

- **Win+Left / Win+Right** - Fix hotkeys not working sometimes: order displays by Windows display number (DISPLAY1 → DISPLAY2 → DISPLAY4); fallback match by display number when stored ID format differs (extended vs simple); when no target/selected display is set, use the game window's display so Win+Left/Right works from current monitor; keep Target Display combo and `target_extended_display_device_id` in sync when user changes selection.
- **GetAdjacentDisplayDeviceId** - Fix match failure when current extended device ID has a different UID (e.g. UID4357) than cached entries (e.g. UID4353/UID4355) for the same display: add fallback that normalizes extended IDs by stripping the UID number so same-display IDs compare equal; only log "Failed to match" when all fallbacks fail; return empty instead of wrong first entry when no match.


## v0.12.14 (2026-02-21)

- **Continue rendering in background** - Fix for games that call SetWindowLongPtr(GWLP_WNDPROC) to subclass the window: intercept that call on our hooked game window, update our chained "original" to their new WNDPROC, and do not call the real SetWindowLongPtr so our detour stays in place and continue rendering keeps working (SetWindowLongPtrW/SetWindowLongPtrA).

## v0.12.13 (2026-02-21)

- **Windows.Gaming.Input** - Auto-suppress Windows.Gaming.Input.dll by default, forcing the game to use XInput so continue rendering in background works with gamepad. Toggle in Advanced tab (default on).

## v0.12.12 (2026-02-21)

- **Win+Down / Win+Up** - Minimize and restore borderless games (handled in ProcessHotkeys when app is in foreground; uses ShowWindow_Direct to prevent spoofing).
- **Win+Left / Win+Right** - Move game to previous/next display (updates Target Display); if window mode is "No changes", automatically switches to Borderless fullscreen so the move is applied.

## v0.12.11 (2026-02-21)

- **Reflex and Vulkan** - Do not suppress NvAPI_D3D_Sleep in Default FPS limiter mode so Reflex works correctly; make Reflex configurable in Vulkan.

## v0.12.10 (2026-02-20)

- **VRR and triggers** - Change -0.05% to only affect VRR cap (not general FPS); add triggers configuration for continuous monitoring.
- **Stability** - Fix crash in Crimson Freedom Demo; fix general crash; reduce log spam.
- **UI** - Add warning that Changing Display is not implemented in "No Changes" mode.
- **Vulkan** - Reorganize Vulkan hooks and loader files.

## v0.12.9 (2026-02-19)

- **Load library hooks** - Add hooks to LdrLoadDll; add LoadPackagedLibrary; monitor modules every 10s for late-loaded modules; log missed modules that loaded in unknown way.
- **Vulkan** - Add Native Frame Pacing for Vulkan; fix DLSS information tab for Vulkan.

## v0.12.8 (2026-02-18)

- **Image adjustment** - Rewritten brightness adjustment; add gamma, saturation, and hue sliders; add AutoHDR.
- **Build** - Fix GitHub build.

## v0.12.7 (2026-02-18)

- **Config** - Migrate Display Commander to TOML; store keyboard hotkeys in Display_Commander\\hotkeys.toml; add chords global file.
- **NVIDIA profile** - Auto refresh after apply as administrator.
- **Code** - Code cleanup; fix GitHub build.

## v0.12.6 (2026-02-18)

- **Controller remapping** - Add controller remapping to main tab; add more remapping keys; fix L preset; fix setting correct render presets.
- **UI** - Add UI to debug controllers; rework main tab UI; update README with correct Discord links.

## v0.12.5 (2026-02-17)

- **NVIDIA profile** - Create NVIDIA profile; add extender XML info; show advanced settings in NVIDIA profile; option to apply NVIDIA profile settings as administrator.
- **System info** - Show VRAM usage (using DXGI adapter); show RAM usage.
- **DLSS** - Add overrides to slDLSSGetOptimalSettings.
- **Experimental** - Add experimental feature to see/edit NVIDIA Profile Inspector features.
- **Loading** - Add loading libraries through .dc64/.dc32.
- **Reflex** - Add reflex settings selector to default FPS limiter mode.
- **UI** - UI improvements.

## v0.12.4 (2026-02-17)

- **Install and config** - Move central location to Display_Commander folder; rewrite SetupDC code.

## v0.12.3 (2026-02-16)

- **UI** - UI fixes (advanced tab and related).

## v0.12.2 (2026-02-16)

- **Game launcher** - Add game launcher support.
- **Frame pacing** - Turn on by default "Schedule present start N frame times after simulation start" (improves frame pacing at minimal latency cost).
- **Reflex** - Remove "use Reflex as FPS limiter" option.

## v0.12.1 (2026-02-21)

- **Borderless window minimize/restore** - Support for minimizing and restoring borderless games using standard Windows keybinds (Win+Down to minimize, Win+Up to restore). ShowWindow hook only blocks minimize for bordered windows so borderless Win+Down/Win+Up works (Special-K style, see [SpecialK@fe80f1d](https://github.com/SpecialKO/SpecialK/commit/fe80f1dc06d7360475c689479d0afbe224e0f68a)). Handled in ProcessHotkeys when app is in foreground only; ShowWindow_Direct used to prevent spoofing.

## v0.12.0 (unreleased)

- **DLSS overrides** - Added DLSS override feature with loading from dlss_override folder, Quality Preset setting, internal resolution scale slider, and M–Z presets; re-enabled DLSS-G profile setting; hook to slDLSSGetOptimalSettings
- **DLSS UI** - Show DLSS information tab when DLSS was active at least once; button to force internal resolution / render preset change; option to control DLSS auto-exposure; DLSS indicator on/off via registry
- **Window and Input** - Added Window Control section; moved screensaver settings to Window Control and Continue Rendering to Input Control; borderless fullscreen support for DOOM The Dark Ages; “ugly” cursor when DC opens
- **Native frame pacing** - Added native frame pacing using simulation thread (present-start+present-end); made native frame pacing and sim-start-only defaults; thread tracking; fixed disabling reflex FPS limit and reporting FPS for FG; Force FG + Auto option
- **Performance overlay** - Added frame timeline bar and frame latency bars; refresh pooling rate adjustment; fixed DLSS resolution display; renamed “CPU usage” to “CPU busy”
- **Audio** - Per-channel VU meters on main tab and performance overlay; left/right speaker volume adjustment; Audio info in Audio Control; rewrite stats generation; fixed GetVolumeForCurrentProcess crash and Proton per-channel volume
- **Stability** - Fixed crashes (Proton, NVAPI, 0/100 low-latency mode, log spam); rewritten crash logs with dbghelp hooks and tab logging; stuck-thread checks and process stuck detection; do not load DC when Special K is already loaded
- **Reflex and low latency** - Fixed reflex state clearing and reflex library init in some conditions; fixed low latency mode 0/100 bug; disabled reflex shutdown code
- **DXGI / Vulkan** - Skip dxgifactory hooks when using DXVK; do not hook DXGI swapchain if DX9 is detected; hide kQueryFailedNomedia in DXGI; show flip status in Vulkan
- **VRR and refresh** - No VRR poll shortly after background→foreground; no refresh rate poll when overlay hidden; atomic vrr_status cache; fix refresh rate on main tab
- **HDR** - HDR10 metadata: CTA-861 scale 50000 and correct Rec. 709 primaries; tooltip for HDR metadata override
- **Build and tooling** - Command-line support; DC installed script; warning when DC is loaded too late; version ID in build metadata; bleeding-edge “latest_build” release; updater only when new build is found
- **UI and cleanup** - Removed Experimental features from UI; refactor and UI improvements; FPS limiter tooltip update; reshade runtime > 1 warning; dedicated logger thread; remove dead/unused code; always flush in ReShade API

## v0.11.0 (2026-01-26)

- **Vulkan support** - Added comprehensive Vulkan API support with FPS limiter, frame time statistics, and latency reduction features
- **DLSS information display** - Added DLSS status display on main tab showing active DLSS features, quality preset, scaling ratio, and internal/output resolution
- **Frame Generation status** - Added Frame Generation (FG) status display in performance overlay and main tab when DLSS-G is active
- **Low latency mode** - Added new low latency mode feature with configurable ratio selector for on-present sync latency reduction
- **PCL Stats reporting** - Added PCLStats ETW reporting support for Special K compatibility with configurable enable/disable option
- **ReShade integration** - Added dedicated ReShade tab and Addons tab for managing ReShade packages and addons, with auto-disable of ReShade's clock
- **Multiple loading methods** - Added support for loading Display Commander as version.dll, dxgi.dll, d3d11.dll, or d3d12.dll proxy from Documents folder
- **rundll32 injection support** - Added Start/Stop/StartAndInject/WaitAndInject commands for rundll32.exe injection method
- **Clip cursor feature** - Added "Clip cursor to game window" option (enabled by default) to prevent cursor from leaving game window
- **Window mode improvements** - Added "no changes" window mode as default option and auto-apply resolution change option
- **Performance overlay enhancements** - Added frame time graph for native frames, VRR status display, FG status display, and improved chart scaling
- **VRR detection improvements** - Improved VRR detection reliability and brought back VRR status display in a safe way
- **Reflex status display** - Added reflex status information display in UI
- **Exception handling improvements** - Enhanced crash reporting with AddVectoredExceptionHandler hooks and improved exception handling
- **Logging improvements** - Added DisplayCommander.log rotation, log file location display, and "Open DisplayCommander.log" button in UI
- **UI enhancements** - Added X button to close Display Commander UI, vertical spacing overlay option, and improved tab visibility controls
- **Input blocking fixes** - Fixed game input being blocked under some conditions and improved input handling
- **Stability fixes** - Fixed crashes related to _mm_mwaitx, DXVK crashes, GetRawInputBuffer_Detour, and various other stability issues
- **Code quality** - Removed usage of std::mutex (replaced with SRWLOCK), extensive code cleanup, and removed unused features
- **Build system fixes** - Fixed GitHub Actions build errors by removing __try/__except blocks that caused compilation issues

## v0.10.0 (2026-01-XX)

- **Performance monitoring** - Added experimental Performance tab with detailed timing measurements for overlay draw, present handlers, and frame statistics
- **PresentMon integration** - Added PresentMon thread support with comprehensive debug information and frame analysis
- **System volume control** - Added system volume slider and hotkeys for volume up/down control
- **CPU management** - Added UI to set CPU affinity (number of cores used) and process priority adjustment
- **Limit real frames** - Added limit real frames feature for frame generation to all FPS limiters
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
