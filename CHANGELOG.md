**When releasing:** the version is stored in one place only. Update `src/addons/display_commander/CMakeLists.txt` (`DISPLAY_COMMANDER_VERSION_MAJOR`/`MINOR`/`PATCH`). CMake passes these into the build; `version.hpp` uses them and derives the version string. Do not edit `version.hpp` for version numbers. See `VERSION_BUMPING.md` for the bump script.

---

# unreleased

## v0.12.342
- **Native frame pacing for FG: disabled by default** - The "Native frame pacing" option (limits native frame rate when Frame Generation / DLSS-G is active) is now off by default. You can still enable it in the FPS limiter section when native frame pacing is in sync if you want to try improved frame pacing with FG. Details: `native_frame_pacing` default changed to false in main_tab_settings.

## v0.12.341
- **Window Control: Focus button** - A "Focus" button was added next to "Minimize Window" in the Main tab Window Control section. It brings the game window to the foreground and restores it if minimized (ShowWindow SW_RESTORE then SetForegroundWindow). Details: main_new_tab.cpp DrawWindowControls.
- **Window Control: Close button** - A "Close" button was added in the Main tab Window Control section. It requests a graceful close of the game window by posting WM_CLOSE (same behavior as the Games tab Stop button). Details: main_new_tab.cpp DrawWindowControls, PostMessageW(..., WM_CLOSE, 0, 0).

## v0.12.340
- **Standalone Settings window title: show current game** - The independent/standalone settings window title is now "Display Commander - \<game window title\> vX.Y.Z" (e.g. "Display Commander - Hollow Knight v0.12.340") when a game window is known (`g_last_swapchain_hwnd`). When no game window or no title is available it falls back to "Display Commander - Settings (No ReShade) vX.Y.Z". The title is updated each frame so it stays in sync if the game changes its window title. Details: cli_standalone_ui.cpp RunStandaloneSettingsUI — title built from GetWindowTextW(g_last_swapchain_hwnd), SetWindowTextW when title changes.
- **Standalone Settings (No ReShade): same tabs and order as in-game UI** - The "Display Commander - Settings (No ReShade)" window now shows the same tabs in the same order as the UI inside ReShade: Main, Games, Advanced, Hotkeys, Controller, Performance, Performance Overlay, Vulkan (Experimental), ReShade, NVIDIA Profile, Debug. The Games tab was previously missing; it is now included, and the ReShade (addons/shaders) tab was added. Tab order matches the in-game overlay for consistency. Details: cli_standalone_ui.cpp RunStandaloneSettingsUI tab bar; addons_tab.hpp include and DrawAddonsTab call.
- **Hotkeys in independent UI window** - Hotkeys (and exclusive key groups) now work when the independent settings window is focused, not only when the game is in foreground or the ReShade overlay is open. So you can use shortcuts (e.g. toggle overlay, Independent UI toggle, Win+Down) while the standalone settings window has focus. Details: ProcessHotkeys and ProcessExclusiveKeyGroups in hotkeys_tab.cpp treat "independent UI window is foreground" as an allowed condition (same as game in foreground or show_display_commander_ui); Hotkeys tab debug section shows "Independent UI window: In foreground" when applicable.

## v0.12.339
- **Independent UI hotkey: open/focus/minimize (no close)** - The Independent UI hotkey (default PgDn) now has three-way behavior instead of open/close: (1) if the independent window is not open, it opens and gets focus; (2) if the independent window is focused, it minimizes and focus returns to the game; (3) if the game (or another window) is focused, the independent window gets focus. The hotkey no longer closes the window—use the window’s X button or uncheck "Show independent window" in the Main tab to close. Details: hotkeys_tab.cpp independent_ui action uses g_standalone_ui_hwnd, GetForegroundWindow_Direct, g_last_swapchain_hwnd; ShowWindow_Direct(SW_MINIMIZE) and SetForegroundWindow for focus/minimize.
- **OpenGL FPS limiter: skip independent UI window** - When the FPS limiter uses the OpenGL swap-buffers path, swaps from the standalone independent settings window are no longer limited or counted as game presents. The addon resolves the window from the device context (WindowFromDC) and skips FPS limiter, present tracking, and frame-time recording when the swap belongs to the independent UI, so that window stays responsive. Details: wglSwapBuffers_Detour in opengl_hooks.cpp checks hwnd against g_standalone_ui_hwnd and bypasses ChooseFpsLimiter, g_last_opengl_swapbuffers_time_ns update, HandlePresentAfter, HandleOpenGLGPUCompletion, and OnPresentUpdateAfter2 for that window.
- **Independent UI: hotkey** - A new hotkey "Independent UI Toggle" (default: PgDown) opens or closes the standalone independent settings window from the Hotkeys tab. Only active when running under ReShade; you can change or clear the shortcut in the Hotkeys tab. Details: hotkeys_tab.cpp new definition independent_ui; hotkeys_tab_settings hotkey_independent_ui (default "pagedown"); action toggles show_independent_window and calls RequestShowIndependentWindow/CloseIndependentWindow.

## v0.12.338
- **Independent UI: fix closing** - Closing the independent settings window (X or Alt+F4) now correctly unchecks the "Show independent window" checkbox in the ReShade overlay, and the game no longer quits when that window is closed. The window-proc hook ignores the independent UI window so exit logic and other game-window handling are never applied to it. Details: WM_CLOSE in cli_standalone_ui WndProc sets show_independent_window to false when the window is DisplayCommanderSettingsUI and running in ReShade; ProcessWindowMessage in window_proc_hooks returns early for g_standalone_ui_hwnd so WM_CLOSE/WM_QUIT/WM_DESTROY never trigger OnHandleExit for that window.

## v0.12.337
- **Independent UI: more usable** - The standalone "Show independent window" is easier to use: (1) the settings window opens at a larger default size (1000×1600) so more content is visible without resizing; (2) the addon no longer injects overlays or game logic into that window—no performance overlay, present tracking, FPS limiter, or effect logic runs there, so it behaves like a separate settings app; (3) the Main tab tooltip for "Show independent window" shows the window HWND and full window stats (rect, size, class, style) when the window is open, using SetTooltipEx for readable wrapping. Details: default size constants `kStandaloneSettingsWindowDefaultWidth`/`Height` in cli_standalone_ui.cpp; no-inject list in `utils/no_inject_windows` (skip overlay + all ReShade HWND callbacks for independent UI); tooltip in main_new_tab.cpp.

## v0.12.336
- **RAM usage: show game (process) memory** - The Main tab and performance overlay now display RAM as **X (Y) / Z** MiB: X = system physical memory in use, Y = current process (game) working set, Z = total system RAM. This makes it easy to see both total system usage and how much the game is using. If process memory cannot be read, the display falls back to X / Z. Details: main_new_tab.cpp — GetProcessMemoryInfo(GetCurrentProcess(), WorkingSetSize) for Y; tooltip explains all three values.

## v0.12.335
- **Vulkan: re-enabled native frame pacing** - Native frame pacing (Reflex-style FPS limiting via Vulkan latency markers) is enabled again for Vulkan games. The addon can again use the NvLowLatencyVk and/or Vulkan loader paths to apply the FPS limiter when Reflex mode is selected, improving frame pacing on Vulkan. Details: Vulkan hooks and ChooseFpsLimiter integration for reflex_marker_vk_nvll / reflex_marker_vk_loader.

## v0.12.334
- **FPS limiter: show active entry points in tooltip** - Hovering over the "(src: …)" label next to FPS Limiter Mode now shows a list of all FPS limiter entry points (reflex_marker, dxgi_swapchain1, reshade_addon_event, etc.) with per-path status: **Active** (path currently applying the limiter), **OK** (path called in the last ~1s), or **-** (not called recently). Status text is color-coded (green for Active/OK, dimmed for inactive). Makes it easy to see which path is in use and which paths are available without opening the Experimental tab. Details: main_new_tab.cpp — tooltip uses BeginTooltip/EndTooltip with TextColored per line; status from g_chosen_fps_limiter_site and g_fps_limiter_last_timestamp_ns.

## v0.12.333
- **Multi-window exit fix** - TLDR: Ignores window close message if app has multiple windows. When the game has more than one window, closing one window (WM_CLOSE, WM_DESTROY, WM_QUIT) no longer triggers the addon exit handler; exit is only triggered when the last window is closed. Prevents premature shutdown when one of several game windows is closed. Details: window_proc_hooks.cpp — CountOtherProcessWindows() excludes the closing HWND and standalone UI; OnHandleExit only called when no other process windows remain.
- **Show independent window: setting-driven** - The "Show independent window" checkbox in the Main tab is now a persisted BooleanSetting. The checkbox only toggles the setting; the continuous monitoring thread opens or closes the standalone settings window based on that setting, so the window state stays in sync across restarts. Details: show_independent_window in main_tab_settings; main_new_tab uses CheckboxSetting only; continuous_monitoring per-second block calls RequestShowIndependentWindow/CloseIndependentWindow when setting and window state differ.

## v0.12.332 (unreleased)
- **Show Independent window: fixed crash** - Opening the "Show independent window" option from the ReShade overlay no longer crashes. The standalone settings window and all related UI (performance overlay, Display Commander window, tab bar) now use the ImGui wrapper instead of calling ImGui directly, so the correct ImGui context is used and symbol/ABI clashes are avoided. Details: cli_standalone_ui.cpp (RunStandaloneSettingsUI, RunStandaloneGamesOnlyUI, DrawLauncherSettingsTab use ImGuiWrapperStandalone); main_entry.cpp (OnPerformanceOverlay_DisplayCommanderWindow, OnPerformanceOverlay_TestWindow, DrawCustomCursor use ImGuiWrapperReshade); new_ui_tabs.cpp TabManager::Draw and NewUISystem::Draw take IImGuiWrapper&; wrapper gains SetNextWindowBgAlpha, GetWindowPos, GetForegroundDrawList.

## v0.12.331
- **Auto-hide Windows taskbar: Window Control selector** - The auto-hide Windows taskbar option is now a three-way selector in the Window Control section: "No changes" (do not hide), "In foreground" (hide taskbar while game is in foreground), "Always" (always hide taskbar while game is running). Replaces the previous checkbox in the ADHD section. If you had the old checkbox enabled, choose "In foreground" for the same behavior. Details: `TaskbarHideMode` enum in globals.hpp; `taskbar_hide_mode` (ComboSettingEnum) in main_tab_settings; `UpdateTaskbarVisibility(in_foreground, mode)` in utils/taskbar_helper; UI in main_new_tab Window Control section.
- **Auto-hide Windows taskbar: recheck on every background update** - The auto-hide Windows taskbar feature now rechecks visibility whenever the continuous monitoring thread updates the game's foreground/background state, not only in the high-frequency block. The same UpdateTaskbarVisibility call is run after the per-second check_is_background() so that taskbar hide/show stays in sync with background status even when only the 1s path runs (e.g. before high-freq interval has elapsed). Details: continuous_monitoring.cpp — taskbar update block added after check_is_background() in the per-second block.
- **Auto-hide Windows taskbar (when in foreground)** - New option (now in Window Control as selector "No changes" / "In foreground" / "Always"). When "In foreground" or "Always" is selected, the Windows taskbar (main and secondary monitors) is hidden accordingly and shown again when appropriate or when the addon unloads. Details: `taskbar_hide_mode` in main_tab_settings; `utils/taskbar_helper` (UpdateTaskbarVisibility, RestoreTaskbarIfHidden); exit_handler calls RestoreTaskbarIfHidden.
- **Main tab DXGI subsection: Force Flip Discard upgrade** - When the game uses FLIP_SEQUENTIAL, a new "DXGI" subsection appears under Display Settings with an option "Force Flip Discard upgrade". When enabled, the addon upgrades the swap chain to FLIP_DISCARD in OnCreateSwapchainCapture2 (better frame pacing; requires restart). Status line shows "Upgrade applied (FLIP_SEQUENTIAL -> FLIP_DISCARD)" when the upgrade was done. The option is only shown when the last swap chain desc (pre-modification) was FLIP_SEQUENTIAL. Details: `g_last_swapchain_desc_pre` / `g_last_swapchain_desc_post` store desc before/after create; `g_force_flip_discard_upgrade_done` for status; main_tab_settings `force_flip_discard_upgrade`; swapchain_events.cpp FLIP_SEQUENTIAL block; main_new_tab.cpp DrawDisplaySettings_DXGI.

## v0.12.330
- **NvAPI_D3D_SetLatencyMarker: null-argument guard** - The NVAPI latency marker detour now returns early when the device or params pointer is null, forwarding the call to the original API instead of processing. Avoids potential crashes or undefined behavior when callers pass invalid arguments. Details: nvapi_hooks.cpp NvAPI_D3D_SetLatencyMarker_Detour.
- **Advanced tab: remove Enable flip chain checkbox** - The "Enable flip chain" option has been removed from the HDR Display Settings section in the Advanced tab. Details: advanced_tab.cpp DrawHdrDisplaySettings.

## v0.12.329 (2026-03-07)
- **Game default overrides: log once per process** - The "resource not found", "checking against exe", and "No game default override" messages are now emitted at most once per process to avoid log spam when the embedded resource is missing or when many callers check overrides. Details: default_overrides.cpp — atomic flags in LoadFromResource (per failure type) and in EnsureLoaded (exe check block).

## v0.12.328
- **Brightness/AutoHDR ReShade paths: remove when unchecked** - Unchecking "Enable Brightness, AutoHDR and ReShade paths" now removes the Display Commander Shaders and Textures paths from ReShade's EffectSearchPaths and TextureSearchPaths (previously they were only not re-added). ReShade config is updated immediately when the checkbox is toggled. Tooltip updated to state that paths are removed when off. Details: `OverrideReShadeSettings_RemoveDisplayCommanderPaths` in main_entry.cpp; OverrideReShadeSettings calls add or remove based on setting; UI invokes OverrideReShadeSettings on checkbox change; addon.hpp declares OverrideReShadeSettings for UI.

- **Auto color space: DXGI format, skip when already set, debug log** - Auto color space now reads the back buffer format from the DXGI swap chain (GetDesc or GetDesc1) instead of ReShade’s cached swapchain desc, so the correct format is used even when the desc is out of sync. The addon only calls SetColorSpace1 when the current color space does not already match the desired one: it stores the last set color space on the swap chain via SetPrivateData (GUID kDcSwapChainColorSpace) and skips apply when GetPrivateData shows it already matches. Before applying, a debug log line prints the reason (no stored color space, stored size mismatch, or stored color space does not match desired) and the desired vs stored values to help diagnose issues. Details: `AutoSetColorSpace`, `SetSwapChainColorSpace` in swapchain_events.cpp.

## v0.12.327
- **Advanced tab: Unsupported features section** - D3D11 vtable hooks, texture tracking, texture caching (1D/2D/3D), content hash cap, dump textures, and texture stats are now grouped under a collapsible "Unsupported features" header at the bottom of the Advanced tab. Behavior unchanged; only UI location and grouping changed. Details: advanced_tab.cpp DrawAdvancedTabSettingsSection.

## v0.12.326
- **Texture cache: separate 1D/2D/3D stats** - Texture cache stats are now reported per dimension. Advanced tab and performance overlay (Tex stats) show lookups, hit, miss, entries, and stored MiB for 1D, 2D, and 3D caches separately. Details: `TextureCacheDimStats`, `texture_cache_1d`/`texture_cache_2d`/`texture_cache_3d` in `TextureTrackerStats`; dimension-specific record functions (e.g. `TextureCacheLookupRecord1D`); `texture_tracker.hpp`/`texture_tracker.cpp`; Advanced tab and main_new_tab overlay UI.

## v0.12.324
- **Dump textures to DDS (fixed texture dumping)** - Optional "Dump textures" checkbox in the Advanced tab (off by default). When enabled, dumping is passive: only when CreateTexture2D adds a **new** texture to the 2D cache (i.e. cache insert, not on cache hit). Requires D3D11 Texture Caching (2D) and vtable hooks. Files are written to a `dumped_textures` subfolder in the **current game folder** (directory of the process executable) as .dds (DX10 header). Filenames use the texture cache key (content hash), e.g. `tex2d_<16-char-hex>.dds`, so identical content overwrites the same file. 2D texture pixel data is written correctly (D3D11 sets SysMemSlicePitch to 0 for 2D textures; dump uses SysMemPitch × Height for the data size). Details: `dump_textures_enabled`; dump runs inside the TextureCachePut success block in CreateTexture2D_Detour; path from GetCurrentProcessPathW().parent_path() / "dumped_textures"; `utils/dds_texture_dump.cpp` DumpTexture2DToDDS.

## v0.12.326
- **Texture cache key: all subresources and SysMemSlicePitch** - The D3D11 texture cache key now hashes all subresources (MipLevels × ArraySize), not just the first, so multi-mip and texture-array textures get correct keys and no longer risk wrong cache hits. Per-subresource slice size uses SysMemSlicePitch when set, with fallback to SysMemPitch × mip height when zero. Fixed typo `cacheabel` → `cacheable` in the CreateTexture2D detour. Details: `HashTexture2DCacheKey` in d3d11_device_hooks.cpp.
- **dbghelp_loader: include windows.h before dbghelp.h** - Fixed include order in dbghelp_loader.hpp so <windows.h> is included before <dbghelp.h>; dbghelp.h requires Windows types (PSTR, HANDLE, etc.). Resolves Clang build failures. Details: utils/dbghelp_loader.hpp.
- **download_dc_as_addon.bat** - New script in `scripts/dc_service/` that downloads the latest Display Commander addon builds (zzz_display_commander.addon64 and .addon32) from the latest_build release. Only downloads when the release asset is newer (checks via HTTP HEAD: ETag or Last-Modified+Content-Length). Can copy the addon as dc_installer64.dll and run SetupDC via rundll32. Uses curl when available (Windows 10+), falls back to PowerShell.

## v0.12.325
- **Stack trace PDB indicator** - Each line in logged stack traces now shows whether symbol/line info came from a PDB for that frame: `[pdb]` when line information was resolved (SymGetLineFromAddr64 succeeded), `[no pdb]` otherwise. Helps see which modules have debug symbols loaded when diagnosing crashes. Details: `utils/stack_trace.cpp` — per-frame `has_pdb` from GetSourceInfo result, suffix appended to each frame line.

## v0.12.324
- **PresentMon OnEtwEvent: fix C2712 (__try and object unwinding)** - MSVC error C2712 ("Cannot use __try in functions that require object unwinding") is resolved by moving the ETW event body into `OnEtwEventImpl` and keeping `OnEtwEvent` as a thin wrapper that only uses `__try`/`__except` with no C++ objects with destructors. This allows the GitHub (MSVC) build to succeed; behavior and SEH handling are unchanged. Details: `presentmon_manager.cpp` / `presentmon_manager.hpp` — `OnEtwEventImpl` holds CALL_GUARD and the former __try body; `OnEtwEvent` calls it inside __try/__except.

## v0.12.323
- **Brightness and AutoHDR tooltips use SetTooltipEx** - Long tooltips in the Brightness and AutoHDR section now use SetTooltipEx (800px wrap) so they wrap instead of stretching off-screen. Covers the section master switch, Brightness %, colorspace, Swapchain HDR Upgrade, HDR mode, RenoDX, AutoHDR Perceptual Boost, HDR10/scRGB color fix, Auto HDR strength, and Misc (Gamma, Contrast, Saturation, Hue). Details: main_new_tab.cpp.

## v0.12.322
- **Brightness and AutoHDR section master switch** - A checkbox "Enable Brightness, AutoHDR and ReShade paths" (default on) at the top of the Brightness and AutoHDR section controls the whole block: when off, Brightness, AutoHDR, and all related controls are disabled, and Display Commander no longer adds global shader/texture paths (EffectSearchPaths, TextureSearchPaths) to ReShade config. Tooltip explains the behavior; an "Open Shaders/Textures folder" button opens the DC ReShade root folder. Details: `brightness_autohdr_section_enabled` in main_tab_settings; gating in ApplyDisplayCommanderBrightness, ApplyDisplayCommanderAutoHdr, OverrideReShadeSettings_AddDisplayCommanderPaths; UI in main_new_tab.cpp with BeginDisabled/EndDisabled; GetDisplayCommanderReshadeRootFolder() in general_utils for the folder path.

## v0.12.321
- **SetTooltipEx: tooltips with configurable wrap width** - Added `SetTooltipEx` to the base ImGui wrapper so tooltips can use a maximum text width (default 800px), making long tooltips wrap instead of stretching across the screen. Use `imgui.SetTooltipEx("text")` for default width or `imgui.SetTooltipEx(width, "text")` for a custom width. The "When on: one set of 4 sliders..." (XInput) and "Override DXGI Present SyncInterval..." (Main tab) tooltips now use SetTooltipEx. Details: `imgui_wrapper_base.hpp` (SetTooltipExV + overloads), ReShade and standalone implementations.

## v0.12.320
- **PresentMon ETW tracing defaults to off** - The "Enable PresentMon tracing" setting now defaults to false so ETW tracing is off until the user enables it. Details: `enable_presentmon_tracing` in advanced_tab_settings.cpp.
- **Texture stats** - When texture tracking is enabled (Advanced tab), stats show texture count, total/peak memory, and in the overlay: min unique keys, lookups, cache hit, cache miss, entries, cache stored (MiB). Skip reasons (no init, track off, cache off, ppNull, key0, size0) explain why CreateTexture2D did not attempt a cache lookup. Details: texture_tracker, advanced_tab.cpp, main_new_tab.cpp.
- **Texture caching 2D** - Optional D3D11 CreateTexture2D cache (Advanced tab, off by default): cacheable textures (with initial data) are stored by content hash; no per-texture size limit, no eviction. A subsequent CreateTexture2D with the same (desc + initial data) returns the cached texture. Normalized desc hashing; cache-hit handouts are added to the tracker so Release is tracked correctly. Details: d3d11_device_hooks.cpp, TextureCacheGet/Put, HashTexture2DDescNormalized, `d3d11_texture_caching_enabled`.

## v0.12.319
- **Enable D3D11 vtable hooks (Advanced tab)** - Added a checkbox "Enable D3D11 vtable hooks (HookD3D11Device)" in the Advanced tab, placed just above "Track loaded texture size", off by default. This controls installation of D3D11 device vtable hooks (e.g. CreateTexture2D) and is required for D3D11 texture stats. If "Track loaded texture size" is enabled without this option, the UI shows a warning. Details: `enable_dx11_vtable_hooks` in advanced_tab_settings; HookD3D11Device called from api_hooks (D3D11CreateDeviceAndSwapChain, D3D11CreateDevice, D3D11On12CreateDevice) and swapchain_events when enabled.

## v0.12.318
- **Texture stats: total memory, peak, cache misses on overlay and Advanced tab** - Performance overlay and Advanced tab now both show total memory used, peak memory used, and total cache misses for the texture tracker. The Main tab "Tex stats" overlay checkbox is shown only when Advanced → "Track loaded texture size" is enabled, so the overlay option does not appear when texture tracking is off. Details: overlay block in `main_new_tab.cpp` shows current/peak MiB and cache misses; Advanced tab line extended with "Cache misses"; Tex stats checkbox wrapped in `texture_tracking_enabled` check.

- **PresentMon OnEtwEvent: SEH guard and full error capture** - The ETW event callback `OnEtwEvent` is now wrapped in `__try`/`__except` so access violations and other SEH exceptions during event parsing do not crash the addon; the faulty event is skipped and a warning is logged. A call guard (`CALL_GUARD`) was added so this callback is tracked for crash reporting. All SEH exception codes are captured and logged with both the raw code (hex) and a readable name for known types (e.g. ACCESS_VIOLATION, STACK_OVERFLOW). Details: `presentmon_manager.cpp` — `OnEtwEvent` with CALL_GUARD, __try/__except, and GetExceptionCode() switch for exception names.

## v0.12.317
- **Texture memory tracking (optional)** - Optional feature (off by default) that tracks the size of loaded D3D11 textures and hooks their `IUnknown::Release`. When enabled in the Advanced tab, stats show current texture count, current memory (MB), and peak memory with a reset button. Only textures created after enabling are tracked. Details: `utils/texture_tracker.hpp` / `texture_tracker.cpp`; D3D11 CreateTexture1D/2D/3D detours and per-type Release hooks in `hooks/d3d11/d3d11_device_hooks.cpp`; Advanced tab setting `texture_tracking_enabled` and stats UI in `ui/new_ui/advanced_tab.cpp`.
- **Texture tracker miss stats and performance overlay** - Texture tracker records "misses" (Release when the texture was not in the map). Total misses and misses-per-second (exponential moving average, geometric decay 0.9) are exposed. Main tab performance overlay has a "Tex stats" checkbox; when enabled the overlay shows total texture misses and misses/s. Details: `TextureTrackerStats.total_misses`, `misses_per_sec_ema`; `RecordMiss()` in `utils/texture_tracker.cpp`; `show_overlay_texture_stats` and overlay line in `main_new_tab.cpp`.

## v0.12.316 (2026-03-06)
- **IsLoadedWithDLLExtension** - Loader mode now runs only when Display Commander was loaded as a proxy DLL (e.g. dxgi.dll, d3d11.dll) — i.e. the module filename ends with .dll. When loaded as the ReShade addon (.addon64/.addon32), the loader path is skipped. Details: `utils/dc_load_path.hpp` / `dc_load_path.cpp` — `IsLoadedWithDLLExtension(void* h_module)`; `main_entry.cpp` uses it to gate the loader-only branch.
- **Auto color space: skip when RenoDX addon is loaded** - When a ReShade addon whose path contains "renodx-" (e.g. renodx-silenthill2remake.addon64) has been detected, the HDR10/scRGB auto color space fix no longer runs, avoiding conflicts with RenoDX’s own color handling. Details: `AutoSetColorSpace` in `swapchain_events.cpp` returns early when `g_is_renodx_loaded` is true (set when `IsRenoDxAddonPath` succeeds in loadlibrary hooks).

## v0.12.315 (2026-03-06)
- **ID3D11Device vtable indices corrected** - D3D11 device vtable indices now match the Windows SDK: in d3d11.h, ID3D11Device inherits only from IUnknown, so CreateBuffer is index 3, CreateTexture2D is 5, CreateDepthStencilView is 10. Previously wrong indices could hook the wrong methods. Details: `d3d11_vtable_indices.hpp` — indices and static_asserts updated to SDK order.
- **D3D11 device hooks in all creation APIs** - D3D11 device vtable hooks (e.g. CreateTexture2D logging) are now installed when the device is created via `D3D11CreateDevice` or `D3D11On12CreateDevice`, not only `D3D11CreateDeviceAndSwapChain`. Games that use the former APIs now get the same device hooks. Details: `api_hooks.cpp` — `HookD3D11Device(*ppDevice)` added on success in `D3D11CreateDevice_Detour` and `D3D11On12CreateDevice_Detour`.
- **api_hooks: add d3d11_device_hooks include** - Fixed compile error "No member named 'd3d11' in namespace 'display_commanderhooks'" by including `d3d11/d3d11_device_hooks.hpp` in `api_hooks.cpp`.

## v0.12.314 (2026-03-05)
- **Don't set colorspace to sRGB on failure** - When SetColorSpace1 fails, the addon no longer falls back to sRGB; it logs the error and returns.

## v0.12.313 (2026-03-05)
- **D3D11On12CreateDevice hook** - Display Commander now hooks `D3D11On12CreateDevice` in d3d11.dll in addition to `D3D11CreateDevice` and `D3D11CreateDeviceAndSwapChain`. Games that create a D3D11 device on top of D3D12 (D3D11on12) are now intercepted for debug layer, logging, and DXGI factory hook installation, matching ReShade’s coverage. Details: `api_hooks.hpp` / `api_hooks.cpp` — `D3D11On12CreateDevice_pfn`, detour, and install in `InstallD3D11DeviceHooks`; `#include <d3d11on12.h>`.

## v0.12.312
- **DXGI factory vtable enum and CreateSwapChainForComposition hook** - Factory vtable indices are now a single enum (`IDXGIFactoryVTable`) covering IDXGIFactory, IDXGIFactory1, and IDXGIFactory2, aligned with ReShade’s indices (CreateSwapChain 10, CreateSwapChainForHwnd/CoreWindow 15/16, CreateSwapChainForComposition 24). IDXGIFactory2::CreateSwapChainForComposition is hooked so swap chains created for composition (e.g. WinUI/XAML) are tracked and hooked like other factory-created swap chains. Details: `dxgi_present_hooks.hpp` enum; `dxgi_present_hooks.cpp` HookFactory + CreateSwapChainForComposition detour.

## v0.12.311
- **HookFactory: ComPtr, single hook, CreateSwapChainForHwnd/CoreWindow** - HookFactory now uses ComPtr for IDXGIFactory and IDXGIFactory1 so interfaces are released correctly and use-after-free is avoided. Factory hooks are installed only once per process via a static flag. Hooking of IDXGIFactory1::CreateSwapChainForHwnd and CreateSwapChainForCoreWindow is re-enabled so swapchain creation through these paths is tracked. Details: `dxgi_present_hooks.cpp` HookFactory.

## v0.12.310
- **Factory hook crash fix** - Fixed a crash that could occur when hooking into the DXGI factory. Details: `api_hooks.cpp`, `dxgi_present_hooks.cpp` / `dxgi_present_hooks.hpp`.

## v0.12.309 (2026-03-05)
- **CreateDXGIFactory2 fix** - The CreateDXGIFactory2 hook no longer forces IDXGIFactory7 for every caller. It only overrides the requested interface to IDXGIFactory2 when the application asks for IDXGIFactory or IDXGIFactory1; for IDXGIFactory2 and newer (through IDXGIFactory7) the requested interface is passed through to the real API. This avoids breaking applications that expect a specific factory version. Details: `api_hooks.cpp` — use requested riid unless highest_rrid_found <= 1, then override to IDXGIFactory2; removed E_NOINTERFACE for unknown interfaces and forced upgrade to Factory7.

## v0.12.308 (2026-03-05)
- **DXGI hooked by default when loading before ReShade (breaking change)** - When Display Commander loads before ReShade, the addon now hooks into DXGI by default so display and swapchain features work correctly. Previously this path could skip hooking; the new behavior may affect load order or compatibility with other injectors. Details: `api_hooks.cpp` — early return only when `g_hooked_before_reshade` is set.

## v0.12.307 (2026-03-05)
- **Color space default** - The default for the Main tab **Color Space** (brightness/output encode) setting is now **Auto** instead of scRGB(default), so new installs and resets match pipeline behavior by default.

## v0.12.306 (2026-03-05)
- **Dynamic Vibrance - Enable in NVIDIA Control** - Added the NVIDIA driver setting **Dynamic Vibrance - Enable** (0x00980880, group "0.2.0 - Graphic | Post-Process") as a checkbox in the Main tab **NVIDIA Control** → RTX HDR section. When **On**, the two sliders **RTX Dynamic Vibrance - Saturation** and **RTX Dynamic Vibrance - Value** are shown; when **Off**, those sliders are hidden. A tooltip warns that enabling globally affects normal apps and may cause graphic bugs. Details: `nvpi_reference.hpp` NVPI_RTX_DYNAMIC_VIBRANCE_ENABLE_ID; `nvidia_profile_search.cpp` new SettingData entry, `GetRtxHdrSettingIds()`; `main_new_tab.cpp` checkbox + conditional visibility for the two vibrance sliders.

## v0.12.305 (2026-03-05)
- **NVIDIA profile FPS limit moved to NVIDIA Control** - The FPS limiter mode no longer includes "NVIDIA Profile (driver FPS limit)". Driver-based FPS limiting is now configured in the Main tab under **NVIDIA Control**: use the "FPS limit (driver profile)" combo to set the value in the NVIDIA profile for the current game. Restart the game for the change to take effect. Details: removed `FpsLimiterMode::kNvidiaProfile`; FPS limit (driver) UI moved from FPS limiter section to NVIDIA Control; `main_tab_settings`, `main_new_tab`, `swapchain_events` updated.

## v0.12.304 (2026-03-05)
- **Ref settings migration: single source of truth** - All main-tab, advanced-tab, and streamline-tab settings that used "Ref" types (storing values in global atomics) now use non-Ref types and store values directly in the setting object. Main tab: scanline offset, vblank divisor, FPS limits, vsync/prevent-tearing, audio volume/mute/background, no-render/no-present in background, CPU cores, brightness/colorspace/gamma/contrast/saturation/hue, auto HDR strength, and window aspect width now use `IntSetting`/`FloatSetting`/`BoolSetting`/`ComboSetting`; readers use `settings::g_mainTabSettings.<name>.GetValue()` or `SetValue()`. Removed the corresponding global atomics and `s_aspect_width` (replaced by `window_aspect_width.GetValue()`). Advanced tab: shortcut toggles and Reflex (auto-configure, logging, suppress native) are now `BoolSetting`; streamline tab: DLSS override checkboxes are now `BoolSetting`. Added `IntSetting::SetMax()` for the CPU cores slider. No user-visible behavior change; settings load/save and UI work as before.
- Details: `settings_wrapper.hpp` — `IntSetting::SetMax`; `main_tab_settings` / `advanced_tab_settings` / `streamline_tab_settings` — Ref→non-Ref, atomics removed; all readers (swapchain_events, latent_sync_limiter, audio_management, continuous_monitoring, hotkeys_tab, main_new_tab, swapchain_tab, input_remapping, reflex_manager, general_utils, etc.) updated to use settings object.

## v0.12.303 (2026-03-05)
- **Sekiro HDR fix** - Fixed HDR handling for Sekiro: Shadows Die Twice.
- **Combo enum settings: store in settings, not globals** - All main-tab combo enum settings (Window Mode, Reflex modes, Input Blocking, Frame Time Mode, Screensaver Mode, Log Level) now use `ComboSettingEnum` and store their value directly in the setting object instead of syncing to global atomics. Callers read via `settings::g_mainTabSettings.<name>.GetValue()` or helpers `GetCurrentWindowMode()` and `GetMinLogLevel()`. Removed `ComboSettingEnumRef` and the global atomics `s_window_mode`, `g_min_log_level`, `s_keyboard_input_blocking`, `s_mouse_input_blocking`, `s_gamepad_input_blocking`, `s_screensaver_mode`, `s_onpresent_reflex_mode`, `s_reflex_limiter_reflex_mode`, `s_reflex_disabled_limiter_mode`, `s_frame_time_mode`. Details: `settings_wrapper.hpp`/`.cpp` — added `ComboSettingEnum` and `ComboSettingEnumWrapper`, removed `ComboSettingEnumRef`; `main_tab_settings` and all readers updated.

## v0.12.302 (2026-03-05)
- **PresentMon on by default** - PresentMon (ETW-based present and flip mode tracking) is now enabled by default when you open the addon, so flip mode and present stats are available without clicking to enable. You can still turn it off in the Main tab if needed.

## v0.12.301 (2026-03-05)
- **Game default overrides: fixed loading** - Per-game defaults from embedded `game_default_overrides.toml` (e.g. `window_mode = 1` for sekiro.exe) were not applied because TOML parses `[hitman3.exe.DisplayCommander]` as nested tables. The loader now recursively collects leaf tables and builds exe/section keys correctly, so overrides load and apply for matching games. Details: `config/default_overrides.cpp` — `CollectLeafTables`; diagnostic logging (exe checked, resource load result, bounded format for exe name).
- **Game default overrides: window_mode and combo settings** - Combo/enum settings (e.g. Window Mode) now use game default overrides when the key is missing in config: load uses `get_config_value_or_default`, and **Reset to default** uses the effective default (override or constructor default) so reset no longer reverts to 0. Details: `ComboSettingEnumRef::Load()` and `GetDefaultValue()` in `settings_wrapper.cpp`; `window_mode` display name in `default_overrides.cpp`.

## v0.12.300 (2026-03-05)
- **PresentMon exception handling** - PresentMon paths are now wrapped in try-catch so exceptions do not crash the addon. `CreateAndStartPresentMon` and `StopAndDestroyPresentMon` catch and log; `StartWorker` catches during thread setup and leaves manager in a consistent "not running" state; `WorkerThread` catches so a thrown exception in the ETW loop is logged and the thread exits cleanly with status "Crashed"; `EtwEventRecordCallback` catches so a bad event does not propagate to ETW; `CleanupThread` catches so one failed cleanup iteration does not kill the thread. Details: `presentmon_manager.cpp` — exception handling in all entry points and callbacks.
- **CALL_GUARD rename** - Renamed macro `RECORD_DETOUR_CALL` to `CALL_GUARD` everywhere (detour call-site tracking for crash reports). Behavior unchanged.

## v0.12.299 (2026-03-05)
- **PresentMon ETW: thread-safe flip state and debug info** - Fixed a use-after-free when the UI read PresentMon flip state or debug info while the ETW callback was updating the same strings. All shared string state is now stored in `std::atomic<std::shared_ptr<const std::string>>`; readers load the shared_ptr and copy the string, so no pointer is used after the writer updates. PresentMonFlipState and PresentMonDebugInfo are filled safely from this storage. EventTypeEntry string fields use the same pattern. Details: `presentmon_manager.hpp` / `presentmon_manager.cpp` — raw `std::string*` atomics replaced with `shared_ptr<const std::string>`; no `delete` in callback path.

## v0.12.298 (2026-03-05)
- **D3D9 FlipEx: fixed log noise for non-D3D9 games** - Fixed unnecessary logging when the game does not use D3D9. The D3D9 to D3D9Ex upgrade path now returns early for DX11/DX12/Vulkan/etc., so "OnCreateDevice" and "D3D9 to D3D9Ex upgrade disabled" are only printed for D3D9 games. Details: `swapchain_events.cpp` `OnCreateDevice` early return before logging for non-D3D9.
- **D3D9 API version constants** - Replaced magic values 0x9000 and 0x9100 with `display_commander::D3D9ApiVersion` enum (D3D9, D3D9Ex) in `utils/d3d9_api_version.hpp`. Used in swapchain_events, main tab, and experimental tab for create_device version and UI. Details: `utils/d3d9_api_version.hpp`; `swapchain_events.cpp`, `main_new_tab.cpp`, `experimental_tab.cpp`.

## v0.12.297 (2026-03-05)
- **Flip / PresentMon label** - When PresentMon is off, the Main tab Flip line now shows "(click to enable)" instead of "(enable PresentMon if needed)" for the clickable link that enables PresentMon.

## v0.12.296 (2026-03-05) IDXGISwapChain3::SetColorSpace1 returned
- **Wine/Proton detection** - Wine detection is now done on first use and cached inside a function (`IsUsingWine()`), instead of using a global variable set at DLL load. Callers no longer depend on init order; behavior is unchanged for users (audio and per-channel volume still skip under Wine when unsupported).

## v0.12.295 (2026-03-05)
- **Vertical Sync in NVIDIA profile and NVIDIA Control** (placeholder to align changelog with version; see Unreleased for latest) - **Vertical Sync** (0x00A879CF, group "2 - Sync and Refresh") option labels and group are aligned with NvidiaProfileInspectorRevamped CustomSettingNames.xml: Force off, Force on, Fast Sync, Use the 3D application setting, 1/2–1/4 Refresh Rate. The setting appears in the NVIDIA Profile tab (Important list) and on the Main tab under **NVIDIA Control** → Low latency (combo + Default when set). Details: `nvidia_profile_search.cpp` Vertical Sync entry + `GetRtxHdrSettingIds()`; `nvpi_reference.hpp` NVPI_VSYNCMODE_ID; `main_new_tab.cpp` Low latency subsection.

## v0.12.294 (2026-03-05)
- **NVIDIA Control layout (Main tab)** - RTX HDR and Low latency controls use consistent widths: RTX HDR sliders (Contrast, Middle Grey, Peak Brightness, Saturation, RTX Dynamic Vibrance Saturation/Value) have a 300px label column and 500px slider width; Ultra Low Latency and Vertical Sync selectors are 500px wide and start at 500px/600px label column; Max pre-rendered frames slider is 500px wide. Details: `main_new_tab.cpp` `nvidia_slider_label_width`, `nvidia_slider_width`, `low_latency_ull_label_width`, `low_latency_slider_width`, ULL combo width in `draw_combo_or_checkbox`.
- **NVIDIA profile "Default" only when value is set** - Every NVIDIA profile setting (Main tab NVIDIA Control and NVIDIA Profile tab) now shows a **Default** control only when the value is set in the profile. Clicking **Default** removes the setting from the profile (unsets it) so the driver uses the global default. When the value is not set, **Default** is hidden. Applies to combo/checkbox settings and to all sliders (RTX HDR Contrast, Middle Grey, Peak Brightness, Saturation; RTX Dynamic Vibrance Saturation/Value; Max pre-rendered frames). Details: `main_new_tab.cpp` `draw_combo_or_checkbox` and each slider; `nvidia_profile_tab_shared.cpp` Important settings and All driver settings table.
- **RTX Dynamic Vibrance in NVIDIA profile** - Two new NVIDIA driver profile settings are supported: **RTX Dynamic Vibrance - Saturation** (0x00ABAB13) and **RTX Dynamic Vibrance - Value** (0x00ABAB22), both in group "0.2.1 - Graphic | HDR". Each has values 0–100 (steps 0, 5, …, 100) plus Custom (0-100). They appear in the NVIDIA Profile tab (Important list) and on the Main tab under **RTX HDR** when RTX HDR is enabled, as 0–100 sliders. Details: `nvpi_reference.hpp` new IDs; `nvidia_profile_search.cpp` `k_settings_data` +2 entries, `GetRtxHdrSettingIds()`; `main_new_tab.cpp` sliders for both.
- **NVIDIA Control UI redesign (Main tab)** - The Main tab **NVIDIA Control** section is now grouped into three subsections (like "DLSS indicator (Registry)"): **Smooth Motion**, **RTX HDR**, and **Low latency**. RTX HDR - Enable and Smooth Motion - Enable are checkboxes; RTX HDR tuning (Contrast, Middle Grey, Peak Brightness, Saturation) is shown only when RTX HDR is enabled and uses sliders. Max pre-rendered frames is a 0–8 slider in the Low latency subsection. Ultra Low Latency (CPL State, Enabled) remain combos in Low latency. Use global (Default) and Default buttons are kept. Details: `main_new_tab.cpp` — flat table replaced with TreeNodeEx subsections; spec: `docs/specs/nvidia_control_ui_redesign.md`.

## v0.12.293 (2026-03-05)
- **Freestyle Filters - Enable in NVIDIA profile and NVIDIA Control** - The NVIDIA driver setting for enabling Freestyle filters (HexSettingID 0x1075D972, same as Ansel enable) is now exposed as **Freestyle Filters - Enable** with group "0.2.1 - Graphic | HDR" in the NVIDIA Profile tab (advanced list) and in Main tab NVIDIA Control. Matches NvidiaProfileInspectorRevamped CustomSettingNames.xml. Details: `nvidia_profile_search.cpp` — renamed "Ansel enable" entry to "Freestyle Filters - Enable", added `group_name`, normalized option hex values.

## v0.12.292 (2026-03-05)
- **NVIDIA Control: restart warning** - When you change any setting in the Main tab **NVIDIA Control** section (combo, Use global, or Default), or change the FPS limit via **NVIDIA Profile** FPS limiter mode, a warning is shown: "Restart the game for profile changes to take effect." Details: `main_new_tab.cpp` file-scope `s_nvidiaProfileChangeRestartNeeded`, set on successful SetProfileSetting/DeleteProfileSettingForCurrentExe and SetProfileFpsLimit; warning shown below the NVIDIA Control table and below the driver FPS combo in FPS limiter section.

## v0.12.291 (2026-03-05)
- **Internal DRS_RestoreProfileDefaultSetting** - Loader now resolves internal (0x7DD5B261) and public (0x53F0381E) RestoreProfileDefaultSetting; wrapper `DRS_RestoreProfileDefaultSetting(p, hSession, hProfile, settingId)` prefers internal. Not yet used (Default button still uses SetProfileSetting to default value). NPI GetDelegate pattern.
- **Internal DRS_GetSettingNameFromId** - Setting name lookup now uses the internal NVAPI (0x1EB13791) when available, with fallback to public (0xD61CBE6E). Matches NvidiaProfileInspectorRevamped. Details: `nvapi_loader` DRS_GetSettingNameFromIdInternal + wrapper; all GetSettingNameFromId call sites use the wrapper.
- **Internal DRS_DeleteProfileSetting for unset** - "Use global (Default)" / unset now uses the internal NVAPI delete-setting API (0xD20D29DF) when available, with fallback to the public API (0xE4A26362). Some settings cannot be unset with the public API alone (NvidiaProfileInspectorRevamped uses the same internal API). Details: `nvapi_loader` queries both, `DRS_DeleteProfileSetting()` wrapper prefers internal then public; `nvidia_profile_search.cpp` calls the wrapper.
- **Use global (Default) for NVIDIA profile settings** - Each NVIDIA profile setting now has a **Use global (Default)** option in the dropdown (like the NVIDIA app). Selecting it removes the setting from the profile so the driver uses the global default. When a setting is not in the profile, the UI shows "Use global - X (Default)" where X is the default value (e.g. Off). Details: `ImportantProfileSetting.set_in_profile`, backend sets "Use global - X (Default)" when DRS_GetSetting fails; combo in NVIDIA Profile tab (Important/advanced and All driver settings) and Main tab NVIDIA Control prepend "Use global (Default)" and call `DeleteProfileSettingForCurrentExe` when selected.

## v0.12.290 (2026-03-05)
- **Ultra Low Latency on Main tab NVIDIA Control** - **Ultra Low Latency - CPL State** and **Ultra Low Latency - Enabled** are now shown in the Main tab **NVIDIA Control** section (same combo + Default as other profile settings). Details: `GetRtxHdrSettingIds()` includes `ULL_CPL_STATE_ID` and `ULL_ENABLED_ID`; Main tab builds the settings list from both `important_settings` and `advanced_settings` so advanced-only entries in rtx_ids appear.

## v0.12.289 (2026-03-05)
- **Min driver version and Ultra Low Latency - CPL State** - Profile settings can now show a minimum required driver version (e.g. "Requires driver 430.00 or newer") in the tooltip on the NVIDIA Profile tab and NVIDIA Control (Main tab). Added the **Ultra Low Latency - CPL State** driver setting (0x0005F543, Off/On/Ultra) to the advanced list with min version 430.00. Details: `ImportantProfileSetting.min_required_driver_version`, `SettingData.min_required_driver_version` propagated in ReadImportantSettings, ReadAdvancedSettings, ReadAllSettings, GetDriverSettingsWithProfileValuesImpl; tooltip in `nvidia_profile_tab_shared.cpp` and `main_new_tab.cpp`; `nvpi_reference.hpp` `ULL_CPL_STATE_ID`, `k_settings_data` +1 entry.
- **Ultra Low Latency - Enabled** - Added driver setting **Ultra Low Latency - Enabled** (0x10835000, Off/On, min driver 430.00) to the NVIDIA Profile tab advanced list. Details: `nvpi_reference.hpp` `ULL_ENABLED_ID`, `k_settings_data` +1 entry.

## v0.12.288 (2026-03-05)
- **NVIDIA Control (Main tab)** - The Main tab section is renamed from "NVIDIA" to **NVIDIA Control**. It now includes a **Refresh** button to reload profile data, shows **Smooth Motion - Allowed APIs** and **Smooth Motion - Enable** (same as NVIDIA Profile tab), and uses the same "Allow - All [DX11/12, VK]" button for Smooth Motion Allowed APIs. Admin-only settings are shown in warning color with tooltip. Details: `main_new_tab.cpp` (header rename, Refresh button, GetRtxHdrSettingIds() order), `nvidia_profile_search.cpp` (Smooth Motion IDs in GetRtxHdrSettingIds(), keep Allowed APIs when filtering requires_admin).
- **Auto-refresh on first open** - The first time you expand **NVIDIA Control** on the Main tab or open the **NVIDIA Profile** tab, profile data is refreshed automatically so the list is up to date. Details: `main_new_tab.cpp` (static s_nvidiaControlOpenedOnce), `nvidia_profile_tab_shared.cpp` (static s_nvidiaProfileTabOpenedOnce).

## v0.12.287 (2026-03-05)
- **Main tab: hide RTX HDR Debanding and Allow** - RTX HDR - Debanding and RTX HDR - Allow are no longer shown in the Main tab NVIDIA section because they require admin privileges and would show privilege errors. They remain available in the NVIDIA Profile tab. Details: `SettingData.requires_admin` in `nvidia_profile_search.cpp`, `GetRtxHdrSettingIds()` filters out requires_admin settings.
- **NVIDIA Profile tab: color for admin-only settings** - Settings that require admin (e.g. Smooth Motion - Allowed APIs, RTX HDR - Debanding, RTX HDR - Allow) are shown in warning color (orange) in the Important, All settings, and All driver settings tables. Tooltip includes "Requires admin to change." Details: `ImportantProfileSetting.requires_admin`, `nvidia_profile_tab_shared.cpp` uses `TEXT_WARNING` for label when `s.requires_admin`.

## v0.12.286 (2026-03-05)
- **Max Pre-Rendered Frames options and Main tab** - The NVIDIA profile setting "Latency - Max Pre-Rendered Frames" (0x007BA09E) now has correct dropdown options: "Use the 3D application setting", 1–8 (matching NPI CustomSettingNames.xml). It is also shown on the Main tab under the NVIDIA section, after RTX HDR - Saturation. Details: `nvidia_profile_search.cpp` (`k_settings_data` option_values for PRERENDERLIMIT_ID, `GetRtxHdrSettingIds()` includes PRERENDERLIMIT_ID).
- **NVIDIA subheader on Main tab with RTX HDR** - When NVAPI is initialized (NVIDIA GPU), the Main tab now shows a collapsible **NVIDIA** section with RTX HDR profile controls: Enable, Debanding, Allow, Contrast, Middle Grey, Peak Brightness, and Saturation. Same behavior as the NVIDIA Profile tab (combo + Default per setting; changes apply to the driver profile). If no profile exists for the current game, a "Create profile" button is shown. Details: `GetRtxHdrSettingIds()` in `nvidia_profile_search`, Main tab draw after DLSS Information block; spec: `private_docs/specs/main_tab_nvidia_subheader_rtx_hdr.md`.

## v0.12.285 (2026-03-05)
- **Simplified smooth motion controls** - In the NVIDIA Profile tab, Smooth Motion controls are simplified: "Smooth Motion (AFR) [40 series]" was removed; "Smooth Motion - Enable [50 series]" is now "Smooth Motion - Enable"; "Smooth Motion - Allowed APIs [40 series]" shows the current value and a single **Allow - All [DX11/12, VK]** button (no per-API toggles or disallow). Details: `nvidia_profile_search.cpp`, `nvidia_profile_tab_shared.cpp`, `nvpi_reference.hpp`.

## v0.12.284 (2026-03-05)
- **FPS Limiter: NVIDIA Profile mode** - Main tab FPS limiter now has a fourth mode: **NVIDIA Profile (driver FPS limit, requires restart)**. When selected, the limit is read from and written to the NVIDIA driver profile (FPS Limiter V3) for the current game; no in-game limiter runs from the addon. The UI shows a dedicated driver FPS selector (Off or 20–1000 FPS), a restart notice, and profile status. If no profile exists, you can create one with a "Create profile" button. Details: `FpsLimiterMode::kNvidiaProfile`, `GetProfileFpsLimit` / `SetProfileFpsLimit` / `GetProfileFpsLimitOptions` in `nvidia_profile_search`, Main tab FPS limiter section and `swapchain_events` (no in-game limiter when this mode is active).

## v0.12.283 (2026-03-05)
- **FPS Limiter V3 in NVIDIA Profile tab** - The NVIDIA Profile tab now includes **FPS Limiter V3** (driver setting 0x10835002, "2 - Sync and Refresh"). You can view and edit the driver FPS limit (Off or 20–1000 FPS) for the first matching profile from the Important profile settings list. Details: `nvidia_profile_search.cpp` (new `k_settings_data` entry for `FRL_FPS_ID`, `FormatImportantValue` and `GetSettingAvailableValues` handle Off + 20–1000 FPS).

## v0.12.282 (2026-03-05)
- **RTX HDR controls in NVIDIA Profile tab** - The NVIDIA Profile tab now includes full RTX HDR (TrueHDR) controls: Enable, Debanding, Allow, Contrast, Middle Grey, Peak Brightness, and Saturation. You can view and edit these per-profile from the same list as other driver settings. Details: `nvpi_reference.hpp` (new setting IDs), `nvidia_profile_search.cpp` (`k_settings_data` extended with six RTX HDR entries and option values from NPI CustomSettingNames.xml).

## v0.12.281 (2026-03-05)
- **NVIDIA Profile internal settings** - The addon now shows NVIDIA Profile internal settings (e.g. RTX HDR status, TrueHDR controls, Driver Fps limit) when available (not editable).

## v0.12.280 (2026-03-04)
- **Removed old reflex UI**

## v0.12.279 (2026-03-04)
- **VSync override combo width** - The VSync override dropdown on the Main tab (Display settings) no longer stretches too wide; it uses a fixed width so the control is compact. Details: `ComboSettingWrapper` now accepts an optional `combo_width` parameter; both VSync combo call sites pass a width. `settings_wrapper.hpp`/`.cpp`, `main_new_tab.cpp`.
- **Gamma 2.2 decode checkbox removed** - The "Gamma 2.2 decode" checkbox was removed from the Main tab (Brightness and AutoHDR). The effect uniform is always set to 0 (disabled). Details: `main_tab_settings` (setting removed), `main_new_tab.cpp` (UI removed), `main_entry.cpp` (always pass 0 to ExtraGamma22Decode).

## v0.12.278 (2026-03-04)
- **Brightness slider range 0–500%** - Main tab brightness slider maximum increased from 200% to 500% for stronger brightness boost. Tooltips and hotkey descriptions updated. Details: `main_tab_settings.cpp` (FloatSettingRef max 500), `main_new_tab.cpp`, `hotkeys_tab.cpp`, `hotkeys_tab_settings.hpp`, `main_entry.cpp`, `DisplayCommander_Control.fx`.

## v0.12.277 (2026-03-04)
- **Swapchain colorspace default 0 (Auto)** - Swapchain colorspace selector now defaults to Auto instead of scRGB. New users and reset-to-default get pipeline-based decode detection.
- **Swapchain colorspace (decode) selector** - Main tab → Brightness and AutoHDR now has a separate **Swapchain colorspace** combo for decode only (default scRGB). **Color Space** controls encode only. DECODE_METHOD is driven by Swapchain colorspace; ENCODE_METHOD by Color Space. Same split applied to DisplayCommander_PerceptualBoost.fx (AutoHDR). Details: `main_tab_settings` (`swapchain_colorspace`, default 1), `ApplyDisplayCommanderBrightness` / `ApplyDisplayCommanderAutoHdr`.

## v0.12.276 (2026-03-04)
- **Fixed missing tooltips** - Tooltips that were not showing for some settings or controls are now displayed correctly when hovering. Details: `settings_wrapper.cpp`.
- **Settings wrapper: reset-to-default button grouping** - The undo (reset to default) button next to sliders, checkboxes, and combo settings is now wrapped in ImGui `BeginGroup`/`EndGroup` so it stays correctly grouped with the control for layout. Details: `SliderFloatSetting`, `SliderFloatSettingRef`, `SliderIntSetting`, `SliderIntSettingRef`, `CheckboxSetting`, `CheckboxSettingRef`, `ComboSettingWrapper`, `ComboSettingRefWrapper`, `ComboSettingEnumRefWrapper` in `settings_wrapper.cpp`.


## v0.12.275 (2026-03-04)
- **D3D9 Ex factory hook: correct vtable index and detour logic / Fixed Steins;Gate support in No Reshade Mode when loaded as dbghelp.dll** - Fixes hooking to Direct3D 9Ex in games such as Steins;Gate. The CreateDeviceEx vtable slot was wrong (index 17 instead of 20): IDirect3D9Ex adds GetAdapterModeCountEx, EnumAdapterModesEx, GetAdapterDisplayModeEx before CreateDeviceEx, so the correct slot is 20. The CreateDeviceEx detour also had an erroneous early return that skipped present-param upgrades, device callback, and success tracking. D3D9FactoryVTable now lists all IDirect3D9/IDirect3D9Ex factory methods (enum class to avoid name clashes with device VTable). Details: `d3d9_vtable_indices.hpp` (CreateDeviceEx = 20, full factory enum), `d3d9_hooks.cpp` (detour flow fixed, vtable index cast).

## v0.12.273 (2026-03-04)
- **D3D9 No-ReShade: FlipEx factory hook gated by setting** - When running without ReShade, the D3D9 Ex factory (Direct3DCreate9Ex) is only hooked when the experimental "D3D9 FlipEx (no ReShade)" setting is enabled. When the setting is off, the factory is not hooked so D3D9 games are left untouched. Reduces impact when the feature is disabled. Details: `Direct3DCreate9Ex_Detour` in `d3d9_hooks.cpp` checks `d3d9_flipex_enabled_no_reshade` before calling `HookD3D9FactoryVtable`.
- **D3D9 logging** - D3D9 PresentParams logging is commented out to reduce log noise; DisplayModeEx log is null-safe for the prefix parameter. Details: `LogD3D9PresentParams`, `LogD3D9DisplayModeEx` in `d3d9_hooks.cpp`.
- **DllMain init logging** - At process attach the addon logs the executable path and current module path; when DllMain exits early or completes, the exit reason is logged (RefuseLoad, EarlySuccess, LoaderOnly, .NO_RESHADE, .NODC, ReShade register failed, RegisterAndPostInit complete). Helps diagnose load order and early-exit behavior. Details: scope guard in `DLL_PROCESS_ATTACH` in `main_entry.cpp`.
- **Logger: IsInitialized** - `DisplayCommanderLogger::IsInitialized()` and global `IsInitialized()` were added so callers can check whether the logger is ready before logging. Details: `display_commander_logger.hpp`/`.cpp`, `logging.hpp`/`.cpp`.

## v0.12.272 (2026-03-04)
- **Display Commander loadable as dbghelp.dll proxy** - When renamed to dbghelp.dll and placed where a game or tool loads it, Display Commander loads as a proxy: all DbgHelp API exports (SymInitialize, StackWalk64, MiniDumpWriteDump, etc.) are forwarded to the system dbghelp.dll, and the addon/ReShade logic runs as usual. Allows loading DC in scenarios that use dbghelp (e.g. crash reporting, symbol resolution). Details: `proxy_dll/dbghelp_proxy.cpp` (generated), `scripts/specs/dbghelp.spec` from Wine; `scripts/gen_proxy_from_spec.py` extended for Wine dbghelp.spec format (-arch=win32/win64, -import, alias, int64 type).
- **exports.def note for proxy regeneration** - A comment at the top of `proxy_dll/exports.def` documents that proxy export blocks (winmm, d3d11, dbghelp, etc.) can be regenerated from Wine .spec files using `scripts/gen_proxy_from_spec.py`.

## v0.12.271 (2026-03-04)
- **DLL detector mode: copy self to dlls_loaded when .DLL_DETECTOR exists** - If a file named `.DLL_DETECTOR` (or e.g. `.DLL_DETECTOR.off`) exists in the same folder as the Display Commander DLL (current module), at the very start of `DLL_PROCESS_ATTACH` the addon copies itself into a **`dlls_loaded`** subfolder under the same filename and **returns TRUE** from DllMain (no further Display Commander initialization). Used with `copy_dc64_to_wine_proxies.py`: run the script, create `.DLL_DETECTOR` in the game folder, then run the game; after exit, `dlls_loaded` contains one copy per DLL name the game actually loaded. Details: `DllDetectorCopyToLoadedIfEnabled` in `main_entry.cpp`, called first in `case DLL_PROCESS_ATTACH`; when it returns true, DllMain returns TRUE immediately.
- **Reset FPS stats: no longer breaks overlay / frametime graph** - Clicking "Reset Stats" in the performance overlay could race with the overlay and frametime graph readers: the ring buffer head was reset to 0 while readers were still iterating, causing wrong indices (underflow) and garbage or zero data so the frametime graph and FPS measurement stopped working until restart. Readers now take a single snapshot of the ring head and use it for both count and sample indexing, so a concurrent reset no longer corrupts the read. Details: `utils/ring_buffer.hpp` (GetCountFromHead, GetSampleWithHead); all readers in `main_new_tab.cpp` and `continuous_monitoring.cpp` use the snapshot pattern.

## v0.12.270 (2026-03-04)
- **VRR/Reflex cap: actual NVIDIA formula** - The VRR Cap button and refresh-rate monitor threshold now use the **actual NVIDIA formula for the Reflex cap**: **3600 × refresh / (refresh + 3600)** (replacing the previous refresh − refresh²/3600). Example: at 144 Hz the cap is ~138.46 FPS. The same formula is used in the Main tab VRR Cap button and in the latent-sync refresh-rate monitor for "samples below threshold" stats. The **×0.995** headroom is applied only when the Reflex FPS limiter is enabled; otherwise the raw cap is used. Details: `main_new_tab.cpp` (VRR Cap uses `ShouldReflexBeEnabled()` for 0.995), `refresh_rate_monitor.cpp`, `refresh_rate_monitor_integration.cpp`, `refresh_rate_monitor.hpp`.

## v0.12.269 (2026-03-04)
- **D3D11 hooks to d3d11.dll when running before ReShade** - When Display Commander runs as a DLL proxy and installs LoadLibrary hooks before loading ReShade, it now installs hooks to the game’s **d3d11.dll** (D3D11CreateDevice and D3D11CreateDeviceAndSwapChain) when that DLL is loaded. This allows intercepting device/swapchain creation and paves the way for fixing “upgrade to flip swapchain” crashes in the future. When ReShade was already loaded before the addon (e.g. injected), D3D11 hooks are not installed so existing behavior is unchanged. Details: `InstallD3D11DeviceHooks` only skips when `g_reshade_module != nullptr && !g_hooked_before_reshade`; LoadLibrary detour calls `InstallD3D11DeviceHooks` on d3d11.dll load (`api_hooks.cpp`, `loadlibrary_hooks.cpp`).
- **LoadLibrary hooks installed before loading ReShade (DLL proxy) — potentially breaking** - When Display Commander is used as a DLL proxy (e.g. renamed to dxgi.dll) and loads ReShade itself (reshade64.dll/reshade32.dll), LoadLibrary hooks are now installed **before** calling LoadLibrary for ReShade, and ReShade is loaded via **LoadLibraryW_Direct** (bypassing the detour). This ensures the ReShade load is observed by the hook system and preserves GetLastError() on failure. **Breaking:** If you rely on ReShade being loaded before any LoadLibrary hooks are active (e.g. custom load order or tools that assume no hooks during ReShade load), behavior changes: hooks are now in place first. A global flag `g_hooked_before_reshade` (atomic) is set when hooks were installed before we loaded ReShade in this path. Details: `main_entry.cpp` (InstallLoadLibraryHooks + g_hooked_before_reshade before LoadLibraryW_Direct), `loadlibrary_hooks.hpp`/`.cpp` (LoadLibraryW_Direct, g_hooked_before_reshade).

## v0.12.268 (2026-03-04)
- **Improved hooking to D3D11 device** - D3D11 device vtable hooking is no longer hard-disabled. When the game uses D3D11, the addon can hook the D3D11 device (e.g. for future vtable logging) in the same way it hooks the swapchain: from present flow, get the device from the DXGI swapchain via `IDXGISwapChain::GetDevice` and call the existing D3D11 device hook, with fallback to ReShade’s device when the DXGI path is not available. Whether the D3D11 device hook is installed is controlled by the existing **Hook Suppression** setting (suppress D3D11 device hooks): when that is off, hooking runs; when on, it is skipped. Details: `hookToD3D11Device` early return removed; D3D11 branch in `OnPresentUpdateBefore` uses `dxgi_swapchain->GetDevice(IID_PPV_ARGS(&d3d11_device))` then `HookD3D11Device`; fallback still uses `swapchain->get_device()->get_native()`.
- **Proxy DLLs: game can load d3d11.dll + d3d12.dll + dxgi.dll at the same time** - The addon no longer statically links d3d11, d3d12, or dxgi. When used as a proxy (renaming Display Commander to d3d11.dll, d3d12.dll, or dxgi.dll), the game can now load multiple of these proxies simultaneously (e.g. d3d11.dll and dxgi.dll in the same folder), so the addon can hook and work with games that load more than one of these DLLs. Details: CMake no longer links d3d11, dxgi, version, or hid for the addon or Launcher exe.
- **NVAPI: load system nvapi64.dll at runtime (no static link)** - The addon no longer links to nvapi64.lib. It loads the system **nvapi64.dll** (installed with the NVIDIA driver) at runtime via a new NVAPI loader. All NVAPI use (driver version, profile search, VRR/refresh rate, DRS settings, RunDLL profile apply) goes through this loader, so the addon works with whatever NVAPI version the driver provides and avoids import-library dependency. Details: `nvapi_loader.hpp` / `nvapi_loader.cpp` (LoadLibrary, nvapi_QueryInterface, resolve and cache function pointers); `nvapi_init`, `nvidia_profile_search`, `nvapi_actual_refresh_rate_monitor`, `continuous_monitoring`, `main_entry` call through loader; CMake no longer links nvapi64.lib for addon or Launcher exe.
- **Developer Tools: link dependencies list** - In Experimental → Developer Tools, a new collapsible **"Link dependencies (addon DLL)"** section lists the libraries linked into the addon (setupapi, tdh, advapi32, wininet, bcrypt, minhook) with short descriptions. Helps users and developers see which system and static libraries are included. Details: `res/link_libraries.hpp` (maintained list), `experimental_tab.cpp` (TreeNode and bullet list).

## v0.12.267 (2026-03-03)
- **Quick FPS selector: always show /1–/6 at 60 Hz+ (175 Hz fix)** - On high refresh rate displays that don't divide evenly (e.g. 175 Hz), the quick FPS limit buttons no longer hide useful divisor options. When the monitor refresh rate is at least 60 Hz, the selector now always shows the first six divisor options (refresh/1 through refresh/6)—e.g. 175, 87, 58, 43, 35, 29 for 175 Hz—instead of only those that divided evenly or yielded ≥60 FPS. Details: `DrawQuickFpsLimitChanger` uses `max_divisor = 6` and skips the divisibility check when refresh ≥ 60 (`main_new_tab.cpp`).

## v0.12.266 (2026-03-03)
- **NVIDIA profile: RTX HDR and driver-resolved settings** - "RTX HDR - Enable" (and any setting marked resolve-from-driver) now uses the **driver’s** setting ID via `NvAPI_DRS_GetSettingIdFromName`, so it works when the driver exposes the setting under a different ID (e.g. in group 5 like Nvidia Profile Inspector Revamped). The lookup name is stored in NVAPI’s expected format (wide string `L"RTX HDR - Enable"`) to avoid UTF-8 conversion issues. When the driver doesn’t expose the setting, the row shows "Not available for this driver" with the queried name and fallback ID. When adding a new setting to a profile, the addon gets the setting name from the driver (`GetSettingNameFromId`) when possible. Details: `SettingData.resolve_id_from_driver`, `driver_lookup_name_wide`, `ResolveSettingIdByDriverName` (wide + UTF-8 overloads), `FindSettingData` and Read* use resolved ID; SetSetting uses `GetSettingNameFromId` before hardcoded names.
- **SetSetting error: show key and value on NVAPI_SETTING_NOT_FOUND** - When changing an NVIDIA profile setting fails with NVAPI_SETTING_NOT_FOUND, the "Last change failed" message now includes the key and value that were tried (e.g. `[key 0xdd48fb, value 0x1]`) so you can correlate with the driver or NPI. Details: `MakeSetSettingError` in `nvidia_profile_search.cpp`.

## v0.12.265 (2026-03-03)
- **Apply as administrator: fixed setting NVIDIA profile when admin required** - "Apply as administrator" in the NVIDIA Profile tab now works correctly when changing settings that require elevated privileges. The addon passes the **full path** of the game executable to the elevated process; that process looks up the profile by path with `NvAPI_DRS_FindApplicationByName` instead of the current process, so the correct game profile is updated even when the current process is rundll32. The button is shown when a privilege error occurred and a profile path is available; RunDLL trims quoted paths so paths with spaces are handled. Details: `SetOrDeleteProfileSettingForExe` takes full exe path and uses `NvAPI_DRS_FindApplicationByName` directly; UI passes `current_exe_path` to `RunNvApiSetDwordAsAdmin`; `main_entry.cpp` quote trimming for exe path argument.

## v0.12.259 (2026-03-03)
- **NVIDIA profile settings: single k_settings_data, use option_values for presentation** - All important and advanced profile settings are now defined in one `k_settings_data` array (22 entries) with a `SettingData` struct: user_friendly_name, hex_setting_id, default_value, is_bit_field, is_advanced, and **option_values** (value/label pairs). Presentation uses `.option_values` when present: `FormatImportantValue` looks up the value in option_values for the display string; `GetSettingAvailableValues` returns option_values for combo boxes. Removed `k_important_settings` and `k_advanced_settings`; `ReadImportantSettings` and `ReadAdvancedSettings` iterate over `k_settings_data` (first 19 = important, last 3 = advanced). PRERENDERLIMIT and REFRESH_RATE_OVERRIDE keep fallback formatting for numeric/low-latency values. Details: `nvidia_profile_search.cpp` (SettingData.is_advanced, FindSettingData, full k_settings_data, FormatImportantValue/GetSettingAvailableValues/Read* use it).
- **Apply as administrator: result from elevated process** - When you click "Apply as administrator", the elevated rundll32 process now writes its outcome to a temp file (path passed as an extra argument). After the process exits, the addon reads that file and shows the real NVAPI error in the UI (e.g. "NVAPI_INVALID_USER_PRIVILEGE" or "Profile not found") instead of no feedback. The RunDLL entry point accepts an optional fourth argument `resultFilePath` and writes "OK" or "ERROR: &lt;message&gt;" to it; the UI generates a temp path, passes it to the admin command, and the wait thread reads the file and sets the "Last change failed" message. Details: `RunDLL_NvAPI_SetDWORD` (optional result path, fopen_s/fprintf), `RunNvApiSetDwordAsAdmin` (optional `resultFilePath` param), `nvidia_profile_tab_shared.cpp` (temp path, `AdminWaitParams`, wait thread reads file, `s_adminApplyResultMessage`/`s_adminApplyResultReady`).
- **Apply as administrator: show why it failed** - When "Apply as administrator" is used in the NVIDIA Profile tab and the elevated process fails to start, the UI now shows a clear error message instead of no feedback. Reasons include: could not get module/DLL path, or ShellExecuteEx failure with the system message (e.g. "Access is denied", or "The operation was canceled by the user" with a note that UAC was cancelled). Details: `RunNvApiSetDwordAsAdmin` now accepts an optional `outError` string; `main_entry.cpp` fills it on failure (including GetLastError/FormatMessage for ShellExecuteEx); `nvidia_profile_tab_shared.cpp` passes the string and displays it in the existing "Last change failed" area.
- **Swapchain HDR Upgrade warning** - When "Swapchain HDR Upgrade" is enabled (Main tab → Brightness and AutoHDR), a warning is now shown: it may cause a black screen, and for best compatibility users can use "RenoDX Unity mod" for generic SDR→HDR upgrade (for non-Unity games). Details: `main_new_tab.cpp` (TextColored with TEXT_WARNING when the setting is on).

## v0.12.258 (2026-03-03)
- **All driver settings (editable)** - In the NVIDIA Profile tab, a new "All driver settings (editable)" section lists every setting recognized by the current driver (same list as the dump file), including settings not yet set in the profile (shown as "Not set"). You can change values via dropdown or bit-field checkboxes, reset to driver default (Default), or remove from profile (Delete). List is loaded when you expand the section and is cached until you click Refresh or change a setting. A "Show only settings with values set" checkbox (default on) filters the table to only settings that have a value in this profile; turn it off to see all driver settings. **Unknown keys:** settings that are in the profile but not in the driver's recognized list (e.g. deprecated or future IDs) are now included: they show the key (name from profile or hex) and value, with Delete only (no combo/Default). Details: `ImportantProfileSetting.known_to_driver`, `GetDriverSettingsWithProfileValues()` (appends profile-only from EnumSettings), `nvidia_profile_tab_shared.cpp` (collapsible section, table, filter checkbox, unknown-key rows).

- **Driver settings enumeration and dump** - The addon can now query all setting IDs recognized by the current NVIDIA driver (same approach as Nvidia Profile Inspector Revamped). New API: `GetDriverAvailableSettings()` returns (setting ID, name) for every setting the driver supports; `DumpDriverSettingsToFile(path)` writes a text file with ID (hex), name, type, and allowed values per setting. In the NVIDIA Profile tab, a "Dump driver settings to file" button writes `nvidia_driver_settings_dump.txt` next to the addon DLL. Use this to see which settings are valid for your driver (e.g. to find new Smooth Motion or other options) or to compare driver versions. Details: `nvidia_profile_search.hpp` (DriverAvailableSetting, GetDriverAvailableSettings, DumpDriverSettingsToFile), `nvidia_profile_search.cpp` (NvAPI_DRS_EnumAvailableSettingIds, NvAPI_DRS_GetSettingNameFromId, dump loop), `nvidia_profile_tab_shared.cpp` (button and result message).

- **Smooth Motion settings label (591 or below 4000 series)** - In the NVIDIA Profile tab, the two Smooth Motion–related settings ("Smooth Motion (AFR)" and "Smooth Motion - Allowed APIs") now show the suffix "(591 or below 4000 series)" so users know these options apply to driver 591 or pre–4000-series GPUs. Details: `nvidia_profile_search.cpp` (k_important_settings labels only; driver setting name unchanged).

- **Reflex mode: single "Inject Reflex" checkbox** - When the FPS limiter mode is set to Reflex, the "Inject Reflex" checkbox was shown twice (next to the Reflex combo and again under PCL stats). It is now shown only once: next to the Reflex combo when PCL stats reporting is allowed, or as a standalone checkbox when PCL stats is not allowed.

- **NVIDIA Profile tab: setting key in tooltip** - Hovering over a setting name in the "Important profile settings" or "All settings in profile" table now shows a tooltip with the driver setting key (e.g. `Key: 0x101AE763`), so users can correlate with NVIDIA Profile Inspector or documentation. Details: `nvidia_profile_tab_shared.cpp` (tooltip on Setting column), `nvidia_profile_search.cpp` (ReadAllSettings now fills `setting_id` and `value_id` for "All settings" rows).


## v0.12.257 (2026-03-03)
- **DXGI VSync override** - For DXGI (D3D10/11/12) games, the Main tab "VSync & Tearing" section now shows a single **VSync** dropdown instead of the two "Force VSync ON" / "Force VSync OFF" checkboxes. Options: No override, Force ON, FORCED 1/2, FORCED 1/3, FORCED 1/4 (NO VRR), FORCED OFF. The chosen value is applied at **Present** time (runtime, no game restart). Vulkan/OpenGL keep the existing two checkboxes (applied at swap chain creation).

## v0.12.256 (2026-03-03)
- **Main tab: Native FPS checkbox disabled without native Reflex** - The "Native FPS" overlay checkbox is now grayed out (BeginDisabled) when the game does not have native Reflex, since native FPS is derived from native Reflex sleep calls. The tooltip when disabled explains that a game with native Reflex is required.
- **Main tab: DLSS/NGX overlay checkboxes disabled until game uses DLSS/NGX** - The DLSS/NGX overlay block (FG Mode, DLSS Res, DLSS Status, DLSS Quality Preset, DLSS Render Preset) is now grayed out (BeginDisabled) until the addon has seen the game use DLSS/NGX (either a DLSS feature was active at least once or a DLSS DLL is loaded). Avoids enabling overlay options that would show no data. The FG Mode tooltip when disabled explains that a game that uses DLSS/NGX is required.

## v0.12.255 (2026-03-03)
- **Main tab: Flip Status checkbox disabled when PresentMon off** - The "Flip Status" overlay checkbox is now grayed out (BeginDisabled) when PresentMon tracing is not enabled, since flip mode is shown from PresentMon data. The tooltip when disabled explains that PresentMon must be enabled in the Advanced tab.

## v0.12.254 (2026-03-03)
- **Main tab: NVAPI stats subsection refactor** - The NVAPI stats overlay block (VRR Status, VRR Debug Mode, Refresh rate, Refresh rate time graph, Refresh rate time stats plus warning and Refresh poll slider) is now drawn by a dedicated subfunction `DrawNvapiStatsOverlaySubsection`, shortening the main tab and keeping the subsection in one place. The entire subsection is grayed out (BeginDisabled) when NVAPI is not initialized (e.g. non-NVIDIA GPU or init failure), so users cannot enable options that would do nothing. Details: `main_new_tab.cpp` (DrawNvapiStatsOverlaySubsection, nvapi_init.hpp include).

## v0.12.253 (2026-03-03)
- **Main tab: overlay checkboxes grouped with labels** - Performance overlay options on the main tab are now grouped with separators and section labels: FPS & core display, CPU / limiter, Frame timing & graphs, DLSS / NGX, GPU & memory, Misc, then NVAPI stats at the bottom. Makes it easier to find related options (e.g. all frame-timing toggles or all DLSS toggles in one place). Details: `main_new_tab.cpp` (overlay checkboxes reorder and group labels).

## v0.12.252 (2026-03-03)
- **Main tab: NVAPI overlay options at bottom with hiccup notice** - VRR Status, VRR Debug Mode, Refresh rate, Refresh rate time graph, and Refresh rate time stats are now grouped at the bottom of the overlay checkboxes under a clear "NVAPI stats (NVIDIA only)" label. The UI states that these options may cause occasional hiccups and are not available on Intel/AMD or Linux, so users can avoid confusion and expect possible frame-time spikes when enabling them. Details: `main_new_tab.cpp` (overlay checkboxes reorder and subsection).

## v0.12.251 (2026-03-03)
- **Continuous monitoring: fixed constants** - The continuous monitoring triggers and intervals (high-frequency updates, per-second tasks, display cache refresh, screensaver/FPS/volume/VRR/Reflex/auto-apply toggles) are no longer user-configurable. They are now compile-time constants so users cannot change them and break behavior. The "Triggers Settings (for debugging purposes)" section has been removed from the Advanced tab. Details: `continuous_monitoring.cpp` (anonymous-namespace constants), `advanced_tab_settings`, `advanced_tab.cpp` (section removed).

## v0.12.250 (2026-03-03)
- **VRR overlay: "NO NVAPI" when NVAPI unavailable** - When VRR status is shown in the performance overlay but NVAPI VRR data is not available (e.g. non-NVIDIA GPU or NVAPI not initialized), the label now shows "VRR: NO NVAPI" instead of "VRR: Off", so users can tell that the addon is not using NVIDIA's VRR query rather than reporting VRR as disabled.

## v0.12.249 (2026-03-03)

- **VBlank Monitor tooltip: debug details and time-in-state** - The Main tab tooltip for "VBlank Monitor: ACTIVE" and "VBlank Monitor: STARTING" now shows debug-oriented details: current thread phase (waiting for Latent Sync, binding to display, or collecting scanlines), how long the monitor has been in that state (e.g. "in this state 12.34s"), and phase index for correlation with code. Helps diagnose why the monitor might be stuck in a given state. Details: `latent_sync/vblank_monitor.cpp` (phase timestamps, `GetStatusStringForTooltip`), `latent_sync_limiter` (`GetVBlankMonitorStatusString`), Main tab tooltip text.

## v0.12.248 (2026-03-03)

- **D3D11 device vtable logging** - Added optional vtable hooks on ID3D11Device for CreateBuffer, CreateTexture1D/2D/3D, CreateShaderResourceView, CreateRenderTargetView, and CreateDepthStencilView. On first call per method a single log line is emitted; on failures a throttled error is logged and the first failure per method gets extra detail (e.g. desc fields). Install once per process when hooking the D3D11 device from the swapchain init path. Details: `hooks/d3d11/d3d11_device_hooks.cpp`, `d3d11_vtable_indices.hpp`.
- **D3D11 hooks: first-call/first-error flags in detours** - First-call and first-error flags for the D3D11 device detours are now function-local `static std::atomic<bool>` inside each detour instead of file-scope variables, keeping state encapsulated per method.

## v0.12.247 (2026-03-03)

- **Main tab: simplified Flip / PresentMon line** - The Main tab now shows a single "Flip: &lt;mode&gt;" line (Composed, Overlay, Independent Flip) when PresentMon is running; full surface and compatibility details remain in the tooltip. When PresentMon is off, the line shows "Flip: (enable PresentMon if needed)" and clicking the parenthetical enables PresentMon (same as Advanced tab). Specs added: `private_docs/specs/flip_state_main_tab.md`, `private_docs/specs/main_tab_spec.md`.

## v0.12.246 (2026-03-03)

- **Don't load DC into helper/crash-handler processes (bug fix)** - Display Commander now refuses to load into helper/crash-handler processes (e.g. UnityCrashHandler, PlatformProcess.exe), not just UnityCrashHandler64.exe, so the addon does not run in launcher subprocesses or crash reporters.

## v0.12.245 (2026-03-03)

- **NVIDIA driver 595 / Hollow Knight** - Fixed the addon not working with NVIDIA driver 595 when playing Hollow Knight; the game should now run correctly with this driver. Details: Disabled **EnqueueGPUCompletion** in the present path (presentBefore). The call that enqueued GPU fence completion for latency/Reflex timing is no longer invoked there, avoiding compatibility issues. Perf measurement can still suppress it via Experimental → Performance; the present path simply no longer calls it by default.

## v0.12.244 (2026-03-03)

- (none)

## v0.12.243 (2026-03-03)

- **.NODC file** - When a `.NODC` file (or `.NODC.<any extension>`) is present in the game executable directory, Display Commander loads ReShade only and does not register as an addon (proxy-only).
- **.NORESHADE / .NODC matching** - Both flags match by splitting the filename by `.` (like an array): any file whose first segment after the leading dot is `NO_RESHADE`, `NORESHADE`, or `NODC` is recognized (e.g. `.NORESHADE`, `.NORESHADE.off`, `.NODC.disabled`).
- **PresentMon disabled by default** - PresentMon tracing (`EnablePresentMonTracing_defoff`) is now off by default; enable it in the Advanced tab if needed.


## v0.12.242 (2026-03-03)
- **Init stage limit removed** - Removed binary-search debug feature: `ENTER_INIT_STAGE()`, `REACH_MAX_ALLOWED_STAGE()`, `ProcessAttachEarlyResult::MaxStageReached`, and `utils/init_stage_limit.hpp/.cpp`. Init now always runs to completion.
- **WriteToDebugLog replaced with LogInfo** - Removed `WriteToDebugLog`; exit and crash-report paths now call `LogInfo` directly. `exit_handler` no longer exposes `WriteToDebugLog`; `WriteMultiLineToDebugLog` still exists and logs line-by-line via `LogInfo`. `process_exit_hooks.cpp` includes `utils/logging.hpp` and uses `LogInfo` for all former `exit_handler::WriteToDebugLog` calls.
- **Exit handler: use normal LogInfo** - Added `#include "utils/logging.hpp"` in exit_handler.cpp so the exit path logs with `LogInfo` for the initial "[exit_handler] OnHandleExit..." message, making it easier to confirm the standard log path during shutdown.
- **PresentMon: two AddonShutdown stop reasons** - Replaced single `PresentMonStopReason::AddonShutdown` with `AddonShutdownExitHandler` (exit handler / OnHandleExit path) and `AddonShutdownUnload` (main_entry addon unload path). Log message "Stopping worker thread (reason: ...)" now distinguishes exit-handler shutdown from normal DLL unload.

## v0.12.241 (2026-03-03)

- **NVAPI one-time init centralization** - Moved one-time NVAPI initialization into `nvapi/nvapi_init.hpp` and `nvapi/nvapi_init.cpp` as `nvapi::EnsureNvApiInitialized()`. Replaces the previous implementation in an anonymous namespace in `continuous_monitoring.cpp`. All addon code that calls NvAPI now ensures init before use: ReflexManager (ApplySleepMode, SetMarker, Sleep, GetSleepStatus, RestoreSleepMode, Shutdown), VRR status and display-id resolution in continuous monitoring, and the actual refresh rate monitor (`NvAPI_DISP_GetAdaptiveSyncData`). Fixes the missing `display_commander::continuous_monitoring::EnsureNvApiInitialized` symbol that ReflexManager had been calling.

## v0.12.240 (2026-03-03)

- **NVIDIA driver version in logs** - After the first successful NVAPI init, the addon logs the NVIDIA driver version (and optional branch string) via `NvAPI_SYS_GetDriverAndBranchVersion` so logs show e.g. "NVIDIA driver version (NVAPI): 560.94 (branch: r560_94)".

## v0.12.238 (2026-03-02)

- **PresentMon: stop other DC_ ETW sessions even when PIDs exist** - `StopAllDcEtwSessions` and `StopOtherDcEtwSessions` no longer skip DC_PresentMon_&lt;pid&gt; sessions whose process is still running. Only our own session (by PID) is skipped; all other DC_ sessions are stopped so they get cleaned up. Stopping an ETW session does not terminate the process.
- **Fix: crash in DX11 when using SM** - Removed `FlushCommandQueueFromSwapchain` (D3D11 immediate context flush from swapchain); it could crash in DX11 when using ReShade/SM. Present path now relies on existing present/EnqueueGPUCompletion logic only.

## v0.12.236 (2026-03-02)

- **GetSharedDXGIFactory** - Fixed shared DXGI factory not working when used before process attach completed; factory creation is deferred until `g_process_attached` is true so callers (display cache, resolution helpers, VRAM info) no longer get null or invalid factory during early init.
- **NVIDIA submodules** - Updated external/Streamline, external/nvapi, and external/nvidia-dlss to latest remote; discarded local changes in nvidia-dlss.

## v0.12.235 (2026-03-02)

- (none)

## v0.12.234 (2026-03-02)

- (none)

## v0.12.233 (2026-03-02)

- **Launcher: Japanese font support** - Standalone launcher and no-ReShade settings UI now merge Japanese glyphs from a Windows system font (Meiryo, MS Gothic, or Yu Gothic) into the default ImGui font so Japanese text (e.g. game names, paths) displays correctly.
- **Deadlock fix on startup** - Fixed a deadlock that could occur during addon startup; initialization order and locking are adjusted so startup completes without hanging.

## v0.12.232 (unreleased)

- **Launcher: Win32 Jump List for recently launched games** - When running the standalone Display Commander Launcher exe, the taskbar Jump List (right-click the taskbar icon) shows a "Recently launched" category. Items from: (1) Steam games launched from the launcher (steam_launch_history), (2) games in the game launcher registry (last run with Display Commander). Up to 16 items, sorted by most recent. Uses SetCurrentProcessExplicitAppUserModelID so the custom list is shown. Steam items use launcher exe + `--steam-run <appid>` (IShellLink does not accept steam:// URLs); registry items use game exe path + arguments. Clicking a Jump List item launches the game; for Steam the exe handles `--steam-run` and exits after starting the game.
- **Shared DXGI factory: defer until process attach** - `GetSharedDXGIFactory()` no longer creates the shared DXGI factory until `g_process_attached` is true. This avoids calling `CreateDXGIFactory1` (and getting DXGI_ERROR_INVALID_CALL / 0x887a0001) during early init before process attach has completed. Callers (display cache, resolution helpers, VRAM info) already handle a null factory and skip DXGI work.
- **Crash fix: shared DXGI factory** - The shared DXGI factory (used for VRAM info, display enumeration, and resolution helpers) now uses `CreateDXGIFactory1_Direct`, which bypasses the CreateDXGIFactory1 detour and calls the original API. This avoids going through the redirect-to-CreateDXGIFactory2 path and prevents crashes that could occur when the addon created the shared factory via the hooked path.

## v0.12.230 (2026-03-02)

- **Games tab: Add to Favorites / Remove from Favorites** - Right-click context menu on Steam search results now shows "Add to Favorites" or "Remove from Favorites" depending on whether the game is in the favorites list. Favorites are stored in HKCU\\Software\\Display Commander\\SteamFavorites.
- **Games tab: Favorites first, star and highlight** - Steam search list shows favorited games first (then by last opened, then name). Favorites are marked with a ★ prefix and highlighted text color so they are easy to spot.

## v0.12.229 (2026-03-02)

- **Games tab: Hide Game in Steam search** - Right-click context menu on a Steam game in "Launch Steam game" search results now includes "Hide Game". Adds the game to a persisted hidden list (HKCU\\Software\\Display Commander\\SteamHiddenGames); hidden games are excluded from the search list.
- **Games UI: type-to-search and Ctrl+A** - On the Games tab (Steam "Launch Steam game" section), typing anywhere in the tab (without clicking the search box first) now focuses the search box and delivers the typed characters so you can quickly type a game name. Ctrl+A anywhere in the Games tab focuses the search box (then Ctrl+A again selects all text in the box). Uses `SetKeyboardFocusHere` and `IsKeyPressed` on the ImGui wrapper; type-ahead is detected via `!WantCaptureKeyboard` and `InputQueueCharacters` / Ctrl+A.
- **Launcher exe icon** - Display Commander Launcher exe now embeds an application icon (Explorer, taskbar, window title bar). Vector source: `res/launcher_icon.svg` (monitor + play symbol). Build generates `res/launcher_icon.ico` via `res/make_launcher_icon.ps1` (ImageMagick if available, else .NET fallback). Icon resource ID 1; window class sets `hIcon`/`hIconSm` in exe build so the window shows the icon.
- **Launcher window position/size persistence** - Launcher (Games + Settings) window position and size are saved to config (Launcher section: WindowX, WindowY, WindowWidth, WindowHeight) when the window is closed and restored on next launch. Size is clamped to 400–4096 for width and 300–4096 for height.
- **Launcher Settings: newest available versions** - Settings tab now shows "Newest available" for both ReShade and Display Commander. A one-time background fetch (ReShade from reshade.me, DC from GitHub releases/latest) runs when the tab is first drawn; installed version and newest available are shown for each. Shows "(checking...)" until the fetch completes, or "(unavailable)" if the fetch failed.
- **Launcher: Games + Settings tabs** - When running via Launcher (rundll32 or exe), the window now has two tabs: "Games" (default) and "Settings". Settings tab includes: font size slider (0.5–2.0, persisted in config section Launcher/FontScale); ReShade (global install) status and version with an **Update** button (downloads latest ReShade Addon to global folder); Display Commander (global install) path and version with an **Update** button (runs CheckForUpdates for GitHub release).
- **Launcher single-instance mutex** - The Games-only UI (rundll32 Launcher and standalone exe) uses a named mutex (`Local\DisplayCommander_LauncherMutex64` / `...32`) so only one instance runs. If another is already running, the new process brings the existing window to focus (ShowWindow SW_RESTORE, SetForegroundWindow) and exits.
- **Games window (Launcher/exe): resize both ways, non-movable, more list rows** - Inner Games window now uses GetClientRect every frame so it grows and shrinks with the outer window. Added NoTitleBar so the tab cannot be dragged. Steam "Launch Steam game" search list uses remaining vertical space (BeginChild 0,0) so more rows are visible when the window is large.

## v0.12.226 (unreleased)

- **Games window (Launcher/exe): stretch to borders** - The standalone Games-only UI window now fills the full client area; position and size are set every frame to the display size so the content stretches when the window is resized. Replaced AlwaysAutoResize with NoResize/NoMove/NoCollapse so the inner ImGui window stays full-client-area.

## v0.12.225 (unreleased)

- **Build: shared object library and common options** - DLL and exe now share an object library (`display_commander_objs`) for all sources except `main_entry.cpp` and `main_exe.cpp`, so common code is compiled once and linked into both targets. A single interface library (`display_commander_common_options`) holds compile definitions, include directories, and compiler flags for both; flags are no longer duplicated. Build time improves when building both targets (e.g. `ninja zzz_display_commander display_commander_exe`).

## v0.12.224 (unreleased)

- **SetupDC and installer UI removed** - Removed the `SetupDC` command from CommandLine (rundll32) and the standalone installer UI (`RunStandaloneUI`). The "Display Commander - Installer" window (Setup tab, Games tab, ReShade install) is no longer available. Use the standalone exe or rundll32 Launcher for the Games-only UI; use ReShade overlay or no-ReShade settings window for configuration.
- **Standalone .exe build** - Project builds both the ReShade addon DLL and a standalone Launcher executable (Display Commander Launcher.exe / Display Commander Launcher32.exe) from the same codebase. EXE uses the same init path as the DLL in no-ReShade mode then runs the standalone settings UI on the main thread. New CMake target `display_commander_exe`; entry in `main_exe.cpp` (wWinMain → RunDisplayCommanderStandalone). DllMain is compiled only for the DLL (guarded with DISPLAY_COMMANDER_BUILD_EXE).
- **Standalone Launcher: Games + Settings** - Display Commander Launcher.exe shows Games and Settings tabs (RunStandaloneGamesOnlyUI superseded by full Launcher UI): running games table, Steam list, Focus/Mini/Rest/Stop/Kill; Settings for font size, install status, etc. Window title "Display Commander - Games"; default size 960×900.
- **Standalone exe: running games list** - Exe sets `g_display_commander_state` to HOOKED so DoInitializationWithoutHwndSafe_Late runs and StartContinuousMonitoring starts; the monitoring thread fills the running-games cache so the Games tab shows other processes with the addon loaded.
- **CI: latest_debug ships exe** - GitHub Actions build-debug job collects Display Commander Launcher_x64.exe (64-bit only) and includes it in the Latest Debug Build release assets and release body.
- **bd.ps1 builds DLL and exe** - `build_display_commander.ps1` builds both `zzz_display_commander` and `display_commander_exe`; `bd_core.ps1` copies Display Commander Launcher.exe to `build\` after a successful build.
- **rundll32 Launcher export** - Addon DLL exports `Launcher` for use with rundll32 (e.g. `rundll32.exe zzz_display_commander.addon64,Launcher`). Opens the same Games-only standalone UI without running the full exe. `RunDisplayCommanderStandalone` is built for both DLL and exe; Launcher is DLL-only.
- **Games tab: Open Games UI (standalone) button** - Button copies or hardlinks the current addon (.addon64/.addon32) to `%LocalAppData%\Programs\Display_Commander`, then launches the Games-only window via rundll32 Launcher. When running as the addon (ReShade) uses the loaded DLL path; when running as the standalone exe uses the addon next to the exe. Shows launch errors in the tab if the operation fails.

---

## v0.12.222 (2026-03-02)

- **Injection service control (32/64-bit)** - Added `GetAddonPathForArch`, `StartService`, `StopService` in `dc_service_status`. Top-right indicators `[32]` and `[64]` in Advanced tab; inline on same line as "(Session-wide...)" in Games tab. Green when running, red when stopped; greyed-out red when addon binary not available (32 or 64-bit). Click to start (rundll32 addon,Start) or stop (TerminateProcess). Addon path resolution: DC load path, AppData, stable/Debug subdirs.
- **Rest button (Games tab)** - Added Rest column: close then relaunch for other processes; launch first then close for current process. Requires exe path and either main window or can_terminate.
- **Games tab moved after Main** - Games tab is now the second tab (right after Main) for quicker access to running games and Steam launch.

---

## v0.12.221 (2026-03-02)

- **Steam games launch section (Games tab)** - Below the running games table: search bar for installed Steam games, list ordered by most recently launched, Start button (launches via steam://run), Open button (opens game folder in Explorer). Launch history stored in `HKCU\Software\Display Commander\SteamLaunchHistory`; "Last opened" column shows relative time (today, this week, this month, X months ago). Right-click context menu with "Open details".
- **Stop button (Games tab)** - Graceful close via WM_CLOSE; works for the current process as well.
- **Running games: Mini, Stop, right-click details** - Renamed Minimize to Mini. Added Stop button. Right-click on title opens context menu with "Open details" (modal shows PID, path, main window, can_terminate).
- **Steam list: duplicate and path fixes** - Deduplicate library paths (case-insensitive) and games by app_id. Normalize install paths to uppercase drive letter (e.g. C:\).
- **Steam games section: removed Refresh button** - List loads automatically on first view; manual refresh no longer needed.
- **Steam games: left-click to launch, details modal for folder** - Removed Dir and Open buttons from the list. Left click on game name launches; right click → "Open details" opens modal with app_id, name, install path, and "Open folder" button.

---

## v0.12.220 (2026-03-02)

- **New Games tab (running games overview)** - Added a Games tab that lists all processes currently running with Display Commander loaded, detected via a per-PID named mutex. Shows PID and game title/executable, auto-refreshes every second while the tab is open, and updates immediately after kill actions.
- **Per-game Focus, Minimize, and Kill controls** - Each row exposes a Focus button that restores and brings the game window to the foreground, a Minimize button that minimizes the game to the taskbar, and a Kill button with confirmation dialog and permission checks (cannot kill the current DC process, and handles access-denied/terminated races gracefully).
- **Running games discovery on dedicated thread** - Mutex access (OpenMutexW) and process/window enumeration run only on the continuous monitoring thread; the Games tab reads from a cached list. Avoids blocking the UI thread and respects the requirement that mutex access be done from a dedicated thread.
- **Focus, Minimize, Kill run off UI thread** - Focus (ShowWindow, SetForegroundWindow), Minimize (ShowWindow SW_MINIMIZE), and Kill (OpenProcess, TerminateProcess) are executed on detached worker threads to avoid deadlock when the overlay is rendered inside the game's window message loop.
- **Kill confirmation modal fix** - Fixed the confirmation dialog not appearing when clicking Kill; the modal now opens and displays correctly.

---

## v0.12.219 (2026-03-02)

- **Subscribe to fewer PresentMon events by default** - PresentMon ETW now subscribes only to DWM by default; DxgKrnl, DXGI, and D3D9 providers are off. Reduces event volume and overhead unless the user enables additional providers in Advanced → PresentMon ETW Tracing.

---

## v0.12.218 (2026-03-02)

- **Cleaned up ReShade loading logic** - ReShade location selection and fallbacks: added debug logging for selected setting, all considered locations (type, version, dir), and chosen result (dir, fallback_selected, fallback_loaded). When setting is "global" and the global base folder has no ReShade DLLs, we now fall back to the highest versioned location (e.g. Reshade\\Dll\\X.Y.Z) and set fallback_selected/fallback_loaded so the UI can show that global was requested but a versioned path was used; avoids returning a path with no DLLs. Logic aligned with "local" fallback behavior.

---

## v0.12.217 (2026-03-02)

- **Refuse to load in UnityCrashHandler64.exe** - When the process executable is `UnityCrashHandler64.exe` (case-insensitive), Display Commander refuses to load (`ProcessAttachEarlyResult::RefuseLoad`) and logs to OutputDebugString. Avoids running the addon in Unity's crash reporter helper process.
- **PresentMon getting stopped** - Fix presentmon not working in Hollow Knight.

---

## v0.12.215 (2026-03-02)

- **PresentMon ETW providers: DxgKrnl and DXGI off by default** - The Advanced tab checkboxes "Subscribe to DxgKrnl" and "Subscribe to DXGI" now default to **off**. "Subscribe to DWM" remains on by default so PresentMon still starts with one provider. Users can enable DxgKrnl/DXGI for more event coverage. Config keys `PresentMonProviderDxgKrnl` and `PresentMonProviderDXGI`; existing configs keep their saved values.

---

## v0.12.214 (2026-03-02)

- **PresentMon: show all ETW sessions from all apps** - In Advanced → PresentMon ETW Tracing, added a collapsible subheader **"Show all sessions from all apps"** (closed by default). When opened, it lists all ETW sessions on the system (from any app) and provides a **Stop** button for each so the user can stop any session. Enumeration uses `GetEtwSessionsWithPrefix(L"")`; empty prefix now returns all sessions instead of returning early. Tooltip warns that stopping a session may affect the app that created it.

---

## v0.12.213 (2026-03-02)

- **TerminateProcess hook: only trigger exit when terminating this process** - The TerminateProcess detour was calling the exit handler (and thus stopping PresentMon, flushing logs, etc.) on every TerminateProcess call. That is wrong when the app is terminating another process (e.g. a child or helper); it caused PresentMon to stop and duplicate "[Exit Handler] Detected exit from PROCESS_TERMINATE_HOOK" logs while the game was still running. We now trigger the exit handler only when the target of TerminateProcess is the current process (GetProcessId(GetCurrentProcess()) == GetProcessId(hProcess)). When the target is another process we only log which process was targeted (target pid, current pid, target image path) for debugging.

---

## v0.12.212 (2026-03-02)

- **Updates: tooltip showing source of version info** - "Up to date" and "Newer version available" in the Updates section now show a tooltip with the source of the version information: ReShade (GitHub crosire/reshade, reshade.me), Display Commander stable (GitHub Display Commander stable releases), and the main-tab version check (GitHub Display Commander stable releases). Spec: `docs/ui_specs/updates_ui_spec.md`.

---

## v0.12.211 (2026-03-02)

- **Game default config override by exe** - Per-game default overrides for Display Commander settings: when a setting is not yet in the game's DisplayCommander.toml, the addon uses values from an embedded `game_default_overrides.toml` (e.g. `hitman3.exe` → ContinueRendering = 1). Overrides apply only when the key is missing (never overwrite existing config). Main tab shows an info banner when overrides are in use, with a tooltip listing active overrides and an "Apply to config" button to persist them to DisplayCommander.toml. Logs once at load whether an override was found for the current exe.

---

## v0.12.210 (2026-03-02)

- **Addon directory DLL loading** - Added support for loading DLL files from the game (addon) directory **after** ReShade is registered. Extensions **.dc64 / .dc32 / .dc / .asi** are loaded before ReShade; **.dc64r / .dc32r / .dcr** are loaded after ReShade so they can use ReShade APIs. DLLs are loaded in a non-blocking way: the originals can be replaced while the game is running (e.g. update `renodx-hollownight.addon64.dcr` to a newer version); the new version is loaded on the next game start.

---

## v0.12.209 (2026-03-02)

- **HDR10 / scRGB color fix** - "Auto adjust color space" was renamed to **"HDR10 / scRGB color fix"** and applies when the back buffer is 10-bit HDR10 (R10G10B10A2) or 16-bit FP scRGB (R16G16B16A16): sets DXGI swap chain and ReShade color space to HDR10 (ST2084) or scRGB (Linear) respectively. No change for 8-bit (SDR). Improves compatibility with RenoDX HDR10 mode. Config key unchanged; existing configs keep their saved value.

---

## v0.12.208 (2026-03-02)

- **Start without ReShade** - Display Commander now starts with the standalone (independent) UI when ReShade is not found, same as when `.NORESHADE` is present. After attempting to load ReShade, if it is still not loaded we set no-ReShade mode and run the usual standalone init so the game runs and the user can open Display Commander’s UI and download ReShade from there.

---

## v0.12.207 (2026-03-02)

- **Window mode default: No changes** - Default window mode is now "No changes" (kNoChanges) instead of "Prevent exclusive fullscreen / no resize". New users and fresh config get no window-mode intervention; existing configs keep their saved value. Config key `window_mode` unchanged.
- **Restart warning when enabling fullscreen prevention** - When the user switches from "No changes" to any other window mode (e.g. "Prevent exclusive fullscreen / no resize"), the Main tab shows a warning: "Restart may be needed for preventing fullscreen." The warning appears only if the user had "No changes" at some point in the session, then changed away from it.

---

- **NVAPI fullscreen prevention removed** - Removed the NVAPIFullscreenPrevention module and all related code: `nvapi_fullscreen_prevention.cpp`/`.hpp`, NVAPI Settings UI block in Advanced tab (auto-enable for games, NVAPI Library status), `nvapi_auto_enable_enabled` setting, `CheckAndAutoEnable()`/`Cleanup()` call sites from main_entry and swapchain_events, and helpers `IsGameInNvapiAutoEnableList`/`GetNvapiAutoEnableGameStatus`/`GetCurrentProcessName` from general_utils. The "Game restart required to apply NVAPI changes" message now appears in the AntiLag 2 / XeLL (fakenvapi) section when toggling fake NVAPI. Unused includes removed from advanced_tab (`<algorithm>`, `<cstring>`, `<set>`). Docs updated: NVAPI_FUNCTIONS_LIST, KNOWN_ISSUES, nvidia_profile_search_simplification_plan (historical note). Window mode and DXGI/D3D9 "prevent exclusive fullscreen" behavior (ShouldPreventExclusiveFullscreen, SetFullscreenState detour) unchanged.

---

## v0.12.205 (2026-03-01)

- **Auto adjust color space: default on, moved to Main tab** - "Auto adjust color space" (renamed from "Auto color space") is now on by default. The checkbox was moved from Advanced → HDR and Display Settings to Main tab → Brightness and AutoHDR, directly under "AutoHDR Perceptual Boost" (shown only for DXGI). Tooltip clarifies that both DXGI swap chain and ReShade color space are set to match the back-buffer format (HDR10/scRGB/sRGB). Config key `AutoColorspace` unchanged; existing configs keep their saved value.

---

## v0.12.204 (2026-03-01)

- **Manual color space removed** - Removed the manual color space selector (combo) and related logic. Color space is now set only when "Auto color space" is enabled (format-based: R10G10B10A2 → HDR10, R16G16B16A16 → scRGB, R8G8B8A8 → sRGB). Removed: `GetManualColorSpaceIndex`/`SetManualColorSpaceIndex`, manual color space setting, manual table and cache, and the Advanced tab "Color space" combo. DXGI swap chain wrapper `SetColorSpace1` no longer overrides the color space; it forwards the game's request to the original swap chain.

---

## v0.12.203 (2026-03-01)

- **Background detection runs at least once** - `check_is_background()` no longer returns early when the swapchain window is null. Foreground/background detection and the update of `g_app_in_background` and `g_last_foreground_background_switch_ns` now always run at least once, so downstream logic (e.g. NVAPI VRR cache in `Every1sVrrStatus`) gets an initial timestamp and can run. Clip-cursor and apply-window-change actions remain gated on a non-null swapchain window.
- **AutoHDR default colorspace** - Fixed default colorspace for AutoHDR to auto.  (AutoHDR didn't work properly with HDR10/SDR)

---

## v0.12.201 (2026-03-01)

- **Updates UI redesign (Main tab)** - ReShade and Display Commander sections simplified: "Prefer global ReShade" / "Prefer global DC" checkboxes (config: `ReshadeSelectedVersion`, `dc_selector_mode`); version status green when up to date, red when newer available (tooltip shows current → new version). ReShade download overwrites single global folder; version combo remote-only (GitHub + reshade.me). DC: newest stable and newest debug fetched automatically; latest debug version parsed from release body ("Version in binaries"); no "Check debug" button. Open folder buttons: tooltips show full path (ReShade global, DC global). Spec: `docs/ui_specs/updates_ui_spec.md`; task: `docs/tasks/reshade_dc_updates_redesign.md`.
- **Display Commander preference order** - When "Prefer global DC" is off, load resolution order is: (1) local addon (game folder with dc64/dc32 addon), (2) global, (3) local proxy (folder of the loader when it is a proxy DLL, e.g. dxgi.dll/winmm.dll). `GetDcDirectoryForLoading(void* current_module)` takes optional loader HMODULE for proxy fallback. Tooltip explains the order and that the checkbox switches to global > local.
- **Local DC version and Local Proxy DC version (Updates)** - "Local DC version" shows the version in the game folder only when the dc64/dc32 addon is present there; otherwise shows **None** with tooltip: addon missing, install there or use proxy/prefer global. New line "Local Proxy DC version" shows the version in the proxy DLL folder (e.g. dxgi.dll, winmm.dll) when loaded as a proxy. Helpers: `GetLocalDcAddonDirectory()`, `GetDcProxyDirectory()` (enumerates modules, prefers proxy in game folder).
- **Stable versions: latest only + cached list** - Removed API that listed all stable versions from GitHub. Updates UI now shows only the latest stable from GitHub (`releases/latest`) and a list of cached local versions under `Display_Commander/stable/X.Y.Z`. Stable download is a single "Download latest stable" button (fetches latest then saves to `stable\`). Replaced `FetchDisplayCommanderReleasesFromGitHub` with `FetchLatestStableReleaseVersion`.
- **DC stable cache folder renamed to stable** - Display Commander stable cache path changed from `Display_Commander/Dll` to `Display_Commander/stable`. All load paths, version_check downloads, loader check, and UI labels updated. Existing `Dll\` folders are no longer read; users can move `Dll` to `stable` once or re-download.
- **Detailed fetch errors** - `DownloadTextFromUrl` now accepts optional `out_error` and sets detailed messages (e.g. "Connection failed (error 12029)", "Empty response"). DC and ReShade fetch paths pass it through; UI shows the detailed error instead of generic "Fetch failed". CheckForUpdates also stores the detailed error in version check state.
- **Download errors in logs** - DC stable and DC debug download failures are logged with `LogError` (e.g. "DC stable download: ...", "DC debug download: ...") in addition to being shown in the UI.
- **TryHardLinkOrCopyFile for copy-then-load** - `CopyCurrentVersionToDll` and `CopyCurrentVersionToGlobal` in version_check now use `TryHardLinkOrCopyFile` (hard link when possible, else copy). `general_utils.hpp` documents that any "copy then load" path should use this helper.
- **DirectoryHasDcAddonDlls** - Removed redundant `dir.empty()` check; logic only depends on presence of addon files.
- **Display Commander "Up to date"** - Updates section now shows green "Up to date" or red "Newer version available" for Display Commander (like ReShade), comparing current DC version with latest stable from GitHub; tooltip shows current → latest when newer is available.
- **VRR Debug Mode tooltip** - Tooltip for the VRR Debug Mode checkbox now explains each NVAPI field from `NvAPI_Disp_GetVRRInfo` (NV_GET_VRR_INFO): enabled, req, poss, in_mode (per NVIDIA NVAPI documentation).

---

## v0.12.200 (2026-03-01)

- **Display Commander selector and upgrades** - New source selector: Local (injection DLL), Global (base path), Debug (Debug\X.Y.Z), Stable (Dll\X.Y.Z). Config: `dc_selector_mode`, `dc_version_for_debug`, `dc_version_for_stable`; legacy `DcSelectedVersion` is migrated once to stable mode + version. Debug builds install to `Debug\X.Y.Z` via "Download to Debug"; stable to `Dll\X.Y.Z`. Loader skips central load when mode is Local; treats runs under both Dll\ and Debug\ as versioned (no double load). UI: mode combo, per-mode version combos (Latest + installed), "Set as global" (copy running addon to base), "Copy current to Dll", Fetch/Download for stable, Check/Download for debug. Fallbacks when selected version is missing: latest in same folder, then base.

---

## v0.12.199 (2026-03-01)

- **Unified ReShade load path** - Single list of all ReShade locations (`GetReshadeLocations`) with type enum: Local (game folder), Global (fixed base), SpecificVersion (Dll\X.Y.Z). Selection via `ChooseReshadeVersion` so override (e.g. 6.7.3) is always respected even when the game folder has a different version (e.g. 6.7.2). Removed "try CWD first" load; ReShade is loaded only from the chosen directory.
- **ReShade config: "global" and "local"** - Config now uses keyword `"global"` for the fixed base folder (replaces empty string); added `"local"` (prefer game-folder ReShade, else global, else latest). Legacy empty config is migrated to `"global"`.
- **ReShade version display** - Strip trailing dot from versions parsed from reshade.me (e.g. "6.7.3." → "6.7.3") so the download-another-version list and UI show clean version strings.

---

## v0.12.198 (2026-03-01)

- **RenoDX: disable and hide Swapchain HDR Upgrade** - When a ReShade addon whose name contains "renodx" (e.g. renodx-hollowknight.addon64, rennodx-silenthill2remake.addon64) is loaded, Display Commander sets `g_is_renodx_loaded`, auto-disables the Swapchain HDR Upgrade setting, skips HDR upgrade logic in create/init swapchain and create_resource_view, and hides the "Swapchain HDR Upgrade" checkbox and HDR mode combo in the Main tab so the option is not shown when RenoDX is loaded.

---

## v0.12.197 (2026-03-01)

- **Main tab Updates: DC and ReShade via selectors only** - In the Main tab Updates section, both Display Commander and ReShade are now controlled by a single version selector each (no override, latest installed, or a specific X.Y.Z); load source and DC loader behaviour (load from Dll when Latest or X.Y.Z is selected) follow from that choice.
- **ReShade version list and auto-backup** - The ReShade version list is supplemented with the latest version from reshade.me (fetched once per session), and the currently loaded ReShade is auto-copied to Reshade\\Dll\\X.Y.Z when the Updates section is opened.

---

## v0.12.196 (2026-03-01)

- **ReShade version selection simplified** - ReShade load config now uses a single value like Display Commander: `""` (no override / base folder), `"latest"` (Reshade\Dll\highest), `"X.Y.Z"` (Reshade\Dll\X.Y.Z), or `"no"` (do not load ReShade). Removed Local/Shared/Specific version combo and shared path. Main tab ReShade: single combo "No ReShade", "No override", "Latest installed", then installed versions.
- **ReShade auto-backup** - When the Updates → ReShade section is opened and ReShade is loaded, the currently loaded ReShade is copied to `Reshade\Dll\X.Y.Z` (version from DLL) once if that folder does not already contain the DLLs and the loaded path is not already under Reshade\Dll\.
- **Load DC from Dll: no separate checkbox** - Removed "Load DC from Dll folder on start" checkbox. Loader behavior is implied by the Display Commander version selection: when "Latest installed" or a specific "X.Y.Z" is selected and the instance is loaded from root/game (not under Dll\), it acts as loader and loads the addon from Dll\version; "No override" means run as addon from current location.
- **Override ReShade location removed** - Removed "Override ReShade location" checkbox and path; ReShade is loaded only from the selected source (no override / latest / X.Y.Z).

---

## v0.12.195 (2026-03-01)

- **Multi-version: ignore loader when resolving conflicts** - When "Load DC from Dll folder on start" is used, the loader (e.g. WINMM.dll) sets itself to `DC_STATE_DLL_LOADER`. Multi-version detection now queries `GetDisplayCommanderState()` from each other DC module; if the other instance is the loader, it is not treated as a conflict. The addon loaded from `Dll\X.Y.Z` is allowed to run instead of refusing to load.
- **Minimum allowed version to load** - Display Commander now refuses to load if its version is below 0.12.194. Versions older than 0.12.194 log a message and return without hooking.

---

## v0.12.194 (2026-03-01)

- **Latest debug download to Dll\X.Y.Z** - "Check latest (debug)" / "Download to Dll" now extracts the release to a staging folder, reads the version from the downloaded addon DLLs, and moves files to `Dll\X.Y.Z` (version from build) instead of `Dll\latest_debug`.
- **Copy current version to Dll** - Main tab Display Commander: new "Copy current version to Dll" button copies the running addon to `Dll\X.Y.Z` if that version folder does not already contain it.
- **DisplayCommanderState enum and loader mode** - `g_display_commander_state` is now `std::atomic<DisplayCommanderState>`. New state `DC_STATE_DLL_LOADER`: when "Load DC from Dll folder on start" is enabled, the instance loaded from root (or game dir) sets itself to DLL_LOADER, loads the addon from `Dll\<selected version>`, and does not hook; the loaded addon becomes the hooking instance.
- **Load DC from Dll folder on start** - Main tab Display Commander: checkbox (default off). When on, the root/game copy acts as a loader and loads DC from `Dll\X.Y.Z` without installing hooks.
- **Override ReShade location** - Main tab ReShade: checkbox "Override ReShade location" (default off) and path input. When enabled, ReShade is loaded from the given path instead of the selected source (Local/Shared/Specific version).

---

## v0.12.193 (2026-03-01)

- **Display Commander loads even if loaded multiple times** - Addon initialization and load paths tolerate being invoked multiple times (e.g. dxgi.dll + winmm.dll as proxies, or repeated ReShade load); DC registers and runs correctly instead of failing or crashing when already loaded.

---

## v0.12.192 (2026-03-01)

- **Main tab FPS limiter quick selector** - When the FPS limiter checkbox is enabled, a quick FPS limit selector is shown just under the FPS limit slider. When monitor refresh rate is known, it shows "No Limit", divisors of refresh (e.g. 72, 48 for 144 Hz), and "VRR Cap"; when refresh rate is unknown, fixed presets only (No Limit, 30, 60, 120, 144) with no fallback.

---

## v0.12.191 (2026-03-01)

- **Multi-proxy state export** - Added `GetDisplayCommanderState()` export (returns `Undecided`, `PROXY_DLL_ONLY`, `HOOKED`, or `DO_NOTHING`) so multiple Display Commander proxy DLLs (e.g. dxgi.dll + winmm.dll + version.dll) can coexist. On load, each instance scans loaded modules; if another module reports `HOOKED`, this instance becomes `PROXY_DLL_ONLY` (registers as addon but does not install hooks). Only the first instance becomes `HOOKED` and runs multi-version detection and hook installation. Enables using Display Commander as several proxies in the same process without crashing.

---

## v0.12.190 (2026-03-01)

- **Main tab Updates: ReShade and Display Commander** - ReShade "Specific version" now shows installed versions only in the version selector; added "Download another version" (list of non-installed versions + Download). Display Commander update source: Local / Shared folder / Specific version (shared path fixed to `%localappdata%\\Programs\\Display_Commander`). Specific version: installed-only version combo, "Download latest" (to root), "Check latest (debug)" from GitHub `latest_debug` release with "Download to Dll" when not installed, and "Fetch available versions" + download selected to `Dll\\X.Y.Z`. Added "Open folder" buttons to open the ReShade and Display Commander containing folders in Explorer.

---

## v0.12.189 (2026-03-01)

- **CI: Latest Build / Latest Debug release body** - The GitHub Actions workflow now reads the version (MAJOR.MINOR.PATCH) from `CMakeLists.txt` and writes it into the release body for [Latest Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_build) and [Latest Debug Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug). "Version in binaries" and "Commit" on those releases now reflect the built version and the triggering commit instead of a fixed 0.11.0 and stale commit.

---

## v0.12.188 (2026-03-01)

- **g_reshade_module robustness** - `g_reshade_module` is now `std::atomic<HMODULE>` for thread-safe access. It is set in every ReShade load path (main_entry, addon Init, proxy LoadReShadeDll, LoadLibrary hooks) and never overwritten when already set (compare_exchange_strong). When ReShade is loaded via LoadLibrary (e.g. by the game), `OnModuleLoaded` detects ReShade64/32.dll or the ReShadeRegisterAddon export and sets `g_reshade_module`. Optional clear in `OnReshadeUnload` (commented) uses `.store(nullptr)` when re-enabled.

---

## v0.12.187 (2026-03-01)

- **ReShade load source improvements** - When "Specific version" is selected but that version is not installed, Display Commander now loads the highest available version instead of failing; the Main tab shows a warning (e.g. "Loaded ReShade 6.7.3 (selected 6.7.1 was not installed)"). Added "No ReShade" option to the load source selector so the proxy can run without loading ReShade. When ReShade is not found (empty path or DLL missing at configured path), the addon now starts in no-ReShade mode instead of showing an error message box.

---

## v0.12.186 (2026-03-01)

- **Main tab UI refactor** - Add UI for upgrading Reshade from main tab.

---

## v0.12.185 (2026-03-01)

- **ReShade load source** - Add feature to download reshade / choose local / shared reshade folder.

---

## v0.12.184 (2026-03-01)

- **Skip loading ReShade and Display Commander from addon directory when already loaded** - When loading .dc/.dc64/.dc32/.asi DLLs from the addon directory (`ProcessAttach_LoadLocalAddonDlls`), skip `LoadLibrary` for files whose version resource ProductName is "ReShade" (if ReShade is already loaded) or "Display Commander" (avoid loading ourselves or a second copy). Uses `GetFileProductNameW` (version info API) to read ProductName; comparisons are case-insensitive.

---

## v0.12.183 (2026-03-01)

- **Project structure reorganization** - Moved feature-specific modules from addon root into existing subdirs: `gpu_completion_monitoring` → `latency/`; `display_initial_state`, `display_cache`, `display_restore` → `display/`; `rundll_injection`, `rundll_injection_helpers.hpp`, `dbghelp_loader` → `utils/`. Root now holds only cross-cutting pieces (main_entry, addon, globals, swapchain_events, continuous_monitoring, resolution_helpers, process_exit_hooks, exit_handler). All includes updated; `docs/project_structure.md` and folder-by-folder move plan updated.

---

## v0.12.182 (2026-03-01)

- **RunDLL injection entry points moved** - `StartAndInject` and `WaitAndInject` (implementations and exports) moved from `main_entry.cpp` to `rundll_injection.cpp`. New `rundll_injection_helpers.hpp` declares `GetReShadeDllPath`, `InjectIntoProcess`, and `WaitForProcessAndInject` (still implemented in main_entry). Simplifies main_entry and makes future injection refactors easier.

---

## v0.12.181 (2026-03-01)

- **main_entry refactor** - Split the large `DLL_PROCESS_ATTACH` block into DllMain-safe helpers (`ProcessAttach_EarlyChecksAndInit`, `ProcessAttach_DetectReShadeInModules`, `ProcessAttach_LoadLocalAddonDlls`, `ProcessAttach_CheckNoReShadeMode`, `ProcessAttach_TryLoadReShadeFromCwd`, `ProcessAttach_DetectEntryPoint`, `ProcessAttach_TryLoadReShadeWhenNotLoaded`, `ProcessAttach_NoReShadeModeInit`, `ProcessAttach_RegisterAndPostInit`). Split `WaitForProcessAndInject` into `WaitForProcessAndInject_MarkExistingProcesses` and `WaitForProcessAndInject_ProcessSnapshot`. DllMain switch case is now a short, readable sequence.
- **ReShade state via g_reshade_module** - Removed `g_reshade_loaded`; ReShade load state is inferred from `g_reshade_module != nullptr`. All code that checked `g_reshade_loaded` now uses `g_reshade_module`. When ReShade is detected or loaded (module enum, LoadLibrary from cwd, LocalAppData, or already loaded), `g_reshade_module` is set to the ReShade HMODULE so hooks and UI have a single source of truth.

---

## v0.12.180 (2026-03-01)
Remove unused code.

---

## v0.12.179 (2026-03-01)

- **Fixed crash due to invalid arguments** - XInput hook could crash when Steam overlay (or other code) called XInput with an pState being nullptr to check whenever device is connected.

---

## v0.12.178 (2026-03-01)

- **CI: Latest Debug Build** - GitHub Actions now builds a Debug configuration with PDB symbols on every push to main and publishes it as the "Latest Debug Build" release (tag `latest_debug`). README and Continuous Integration section updated with a link to the debug release for debugging and crash analysis.

---

## v0.12.177 (2026-03-01)

- **FPS limiter UI reorganized** - Main tab display settings: FPS limiter section now shows (1) enable checkbox + FPS limit slider, (2) enable background checkbox + background FPS limit slider, (3) FPS limiter mode combo, then a subheader "Advanced FPS limiter settings" with all mode-specific options (Reflex/OnPresent/Latent Sync), Limit Real Frames, No Render/Present in Background, and Quick FPS limit changer. Single `DrawDisplaySettings_FpsLimiter` entry point; VSync & Tearing no longer draws the FPS sliders (moved into FPS limiter block).

---

## v0.12.176 (2026-03-01)

- **FPS limiter: enable checkbox instead of "Disabled" mode** - Replaced the "Disabled" option in the FPS limiter mode combo with a separate "FPS limiter enabled" checkbox (default on). The combo now has three modes only: Default (OnPresent sync), Reflex, and Sync to Display Refresh Rate (Latent Sync). Unchecking the checkbox disables all FPS limiting; the selected mode applies only when the limiter is enabled. Removed `FpsLimiterMode::kDisabled` from the enum; Reflex "when limiter off or Latent Sync" uses the same Reflex combo as before. Safemode sets the checkbox to off instead of the removed Disabled mode. No migration: existing configs with old mode value 3 are clamped to 2 (Latent Sync).

---

## v0.12.175 (2026-03-01)

- **Window mode and prevent fullscreen merged** - Combined the Main tab "Window Mode" dropdown and the Advanced tab "Prevent Fullscreen" checkbox into a single setting. Window Mode now has four options: "No changes" (no prevent fullscreen, no resize), "Prevent exclusive fullscreen / no resize" (new default; prevent fullscreen only), "Borderless fullscreen", and "Borderless windowed" (last two prevent fullscreen and apply resize as before). Removed the separate Prevent Fullscreen option from the Advanced tab. Safemode now sets window mode to "No changes" instead of toggling the removed checkbox. Move-to-display hotkeys (Win+Left/Right, primary/secondary) temporarily switch to borderless fullscreen when in "No changes" or "Prevent exclusive fullscreen / no resize" so the move is applied. No migration; existing saved window_mode values (0/1/2) unchanged.

---

## v0.12.174 (2026-03-01)

- **32-bit build: Ninja preset and parallel builds** - Added `CMakePresets.json` with single-config Ninja preset for 32-bit (`ninja-x86`). `bd32.ps1` / `bd32_core.ps1` now prefer Ninja when `vcvars32.bat` is available (via vswhere), running configure and build in a 32-bit environment for full parallel compilation. Fallback remains Visual Studio generator with MSBuild intra-project parallelism (`CL_MPCount`). Switched from Ninja Multi-Config to single-config Ninja to avoid object-order dependency errors with generated `.rc` files; build type is passed as `CMAKE_BUILD_TYPE` at configure time.

---

## v0.12.173 (2026-03-01)

- **dc_service: ReShade download and DLL in script folder** - `download_dc32_winmm.bat` and `download_dc64_winmm.bat` now download ReShade Addon installer (ReShade_Setup_6.7.3_Addon.exe) to shared folder `Display_Commander/Reshade_Setup` when missing, and ensure `Reshade32.dll` / `Reshade64.dll` are present in the script folder (extracted from the installer when needed). Shared folder avoids redownload when running either script.

---

## v0.12.172 (2026-03-01)

- **Add Swapchain HDR Upgrade (scRGB / HDR10)** - Main tab (Brightness and AutoHDR): checkbox "Swapchain HDR Upgrade" and "HDR mode" selector (scRGB default, HDR10). Upgrades swap chain to HDR on creation for DXGI; requires Windows HDR or HDR-capable display. Credits: approach inspired by [AutoHDR-ReShade](https://github.com/EndlesslyFlowering/AutoHDR-ReShade) (EndlesslyFlowering).

---

## v0.12.171 (2026-02-28)

- **AutoHDR UI** - Warning shown only when AutoHDR is on and backbuffer is 8-bit: "Recommend RenoDX for SDR->HDR swapchain upgrade." Removed RenoDX link button and "only applies" wording; simplified checkbox tooltip.

---

## v0.12.170 (2026-02-28)

- **Windows Gaming Input: remove __try/__except** - Removed MSVC SEH from `HStringToNarrowSafe` in `windows_gaming_input_hooks.cpp` so the addon builds on toolchains that do not support `__try`/`__except`. HSTRING-to-UTF-8 conversion for RoGetActivationFactory logging is unchanged; invalid HSTRING is no longer caught (defensive only).

---

## v0.12.169 (2026-02-28)

- **AutoHDR UI warning and RenoDX link** - Main tab Brightness and AutoHDR: added visible warning that AutoHDR only applies the Perceptual Boost shader and requires an external addon (e.g. RenoDX) to upgrade swapchain from SDR to HDR; updated checkbox tooltip; added "RenoDX (open in browser)" button linking to recommended addon (https://github.com/clshortfuse/renodx). Future: autodownload option for addon.

---

## v0.12.168 (2026-02-28)

- **Bug fixes (detour safety and validation)** - Addressed items from the bug-detection audit: null checks before calling `_Original` in detours (SetWindowLong*, GetModuleHandleEx, Rand_s, SendInput, slUpgradeInterface, D3D9 Present, NvLL Vulkan SetSleepMode, DXGI Present/Present1 early-return paths); null checks for output pointers before calling original API (GetGUIThreadInfo pgui, CreateDXGIFactory2 ppFactory, GetCursorPos lpPoint, GetKeyboardState lpKeyState, GetFullscreenState pFullscreen, WM_WINDOWPOSCHANGED pWp); early return with error when input is invalid (GetModuleHandleEx phModule, Rand_s randomValue, SendInput pInputs when nInputs > 0); REFIID logging fixed via FormatRefIid() in CreateDXGIFactory2/D3D12CreateDevice; ReleaseDC(swapchain_hwnd, hdc) in window_management; WStringToUtf8 size <= 1 guard in audio_management; ApplyDisplaySettingsDXGI LogInfo format/argument count; main_entry snprintf %ls for wide strings; DXGI GetDesc/GetDesc1/CheckColorSpaceSupport and DInput/display_settings/TranslateMessage/DispatchMessage/GetMessage/PeekMessage validation or _Original guards as documented in `docs/tasks/bug_detection_task.md`.

---

## v0.12.167 (2026-02-28)

- **Reflex-only latency** - Removed the LatencyManager abstraction; latency is now Reflex-only. Replaced `g_latencyManager` with `g_reflexProvider` (ReflexProvider). Deleted `latency_manager.cpp` and `latency_manager.hpp` (ILatencyProvider, LatencyTechnology, LatencyConfig). ReflexProvider is the single public API: Initialize, InitializeNative, Shutdown, SetMarker, ApplySleepMode, Sleep, UpdateCachedSleepStatus, GetSleepStatus; marker counting and sleep/ApplySleepMode stats moved into ReflexProvider. SleepStatusUnavailableReason simplified (kNoReflex, kReflexNotInitialized); SleepStatusUnavailableReasonToString moved to globals.hpp. PCLSTATS_DEFINE() moved to reflex_provider.cpp.

---

## v0.12.166 (2026-02-28)

- **Dead code removal** - Removed unused/orphaned API and declarations: GetQPCallingModules(), GetHookIdentifierByName() (timeslowdown_hooks); LatencyManager::GetCurrentTechnology(), GetCurrentTechnologyName(), SetConfig(), GetConfig(), IncreaseFrameId() (latency_manager); ReflexManager::IncreaseFrameId() (reflex_manager); extern GetTargetFps() from latency_manager. Audit and checklist updated (runs 68–78).

---

## v0.12.165 (2026-02-28)

- **Dead code removal** - Too long to count.

---

## v0.12.164 (2026-02-28)

- **Dead code removal** - Removed unused code and one hook module: (1) Orphaned comment `// #define TRY_CATCH_BLOCKS` in `continuous_monitoring.cpp`. (2) `HookSuppressionManager::WasHookInstalled()` (never called). (3) **WinMM joystick hooks** — deleted `hooks/winmm_joystick_hooks.cpp` and `.hpp`; removed WINMM_JOYSTICK from DllGroup, HookType, hook suppression settings, and hook stats UI; removed `InstallWinMMJoystickHooks` call from loadlibrary; removed `HOOK_joyGetPos`/`HOOK_joyGetPosEx` and `InputApiId::WinMmJoystick` from windows_message_hooks and input_activity_stats. WinMM proxy DLL and timeslowdown’s `timeGetTime` (winmm.dll) are unchanged. Audit doc `docs/tasks/dead_code_and_unused_files_audit.md` and checklist updated.

---

## v0.12.163 (2026-02-28)

- **DualSense HID vibration (rumble)** - When "Use DualSense as XInput" is enabled, XInputSetState(0) now drives the first DualSense via HID output report (USB report ID 0x02, rumble flags + left/right motor 0–255). DualSenseHIDWrapper::SetRumble(device_index, left_speed, right_speed) sends the report; Bluetooth rumble (report 0x31 + CRC) not implemented yet. Vibration amplification applies before sending.
- **Vibration test for DualSense** - XInput tab vibration test (Test Left/Right Motor, Stop Vibration) now uses XInputSetState_Detour instead of XInputSetState_Direct so controller 0 uses the same path as in-game (DualSense HID rumble when enabled). Added public XInputSetState_Detour and fallback to XInputSetState_Direct when no XInput DLL is hooked so the test works before any game loads XInput.

---

## v0.12.162 (2026-02-28)

- **DualSense: remove Battery & Power subsection** - Removed the "Battery & Power" collapsing section from DualSense Data in both the DualSense widget (Input Report) and the XInput widget to fix a crash-to-desktop when expanding that section. The subsection displayed battery percent, power state, USB/headphones/mic flags, etc.; the crash was likely from invalid or uninitialized data when opening it.

---

## v0.12.161 (2026-02-28)

- **DualSense HID: background poll thread** - ReadFile_Direct for DualSense now runs on a dedicated background thread instead of the game thread. The thread starts when DualSensePollingOnce (or UpdateDeviceStates) is called at least once. XInputGetState path reads cached state via RunWithDevicesSharedLock; no ReadFile on the game thread after the thread is running. Reduces input latency by moving HID I/O off the critical path.
- **DualSense: devices_lock_** - SRWLOCK protects device list/state: background thread holds exclusive lock while updating from HID; readers use RunWithDevicesSharedLock for shared access. Thread is stopped and joined in Cleanup().

---

## v0.12.160 (2026-02-28)

- **DualSense HID: setBufferCount(2)** - After opening a DualSense HID device we now call `DeviceIoControl(IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS, 2)` (same as Special K). Caps the HID input queue to 2 reports so the "latest" report stays fresher and can reduce latency when reading at high rate or with a background polling thread.

---

## v0.12.159 (2026-02-28)

- **DualSense HID: setPollingFrequency(0)** - After opening a DualSense HID device we now call `DeviceIoControl(IOCTL_HID_SET_POLL_FREQUENCY_MSEC, 0)` (same as Special K). Poll interval 0 means irregular reads: each ReadFile returns the latest report without waiting for a fixed interval, reducing latency when we poll at our own rate.
- **DualSense: HID report rate and "ever called"** - Per-device rate (reports/sec) for the packet-number-changed path; `packet_rate_ever_called` records whether that path has run at least once. DualSense widget device details show "HID reports: X.X/sec" or "HID reports: never" with tooltips.
- **Controller tab: Input polling rates** - New "Input polling rates" section shows XInput GetState(0) calls/sec (rolling 1s) and DualSense HID reports/sec (first device). Uses GetOriginalTickCount64 so time-slowdown does not skew rates.
- **Controller tab: single entry point** - Full Controller tab content is drawn by `DrawControllerTab()` in the XInput widget (Active input APIs, Input polling rates, XInput widget, Remapping widget). New UI calls only this so adding widgets in one place keeps both UIs in sync.

---

## v0.12.157 (2026-02-28)

- **XInputGetState(0) last duration (Event Counters)** - Controller tab → Event Counters now shows "XInputGetState(0) last duration: X.XXX ms" for the most recent call when the game polls controller 0. A RAII scoped timer runs only when `dwUserIndex == 0`; on scope exit a lambda records the elapsed time so the UI can display it. Helps gauge detour overhead.

---

## v0.12.156 (2026-02-28)

- **Dead code removal** - Removed unused/orphaned code: (1) Display Settings Debug tab (`display_settings_debug_tab.cpp`/`.hpp`, never wired into UI). (2) `#if 0` block in `dinput_hooks.cpp` (~165 lines of disabled DirectInput device-state detours). (3) `#ifdef TRY_CATCH_BLOCKS` blocks in `main_entry.cpp` and `continuous_monitoring.cpp` (macro never defined). (4) Unused `ParseDisplayNumberFromDeviceIdUtf8()` in `display_cache.cpp`. Audit doc `docs/tasks/dead_code_and_unused_files_audit.md` updated.

---

## v0.12.155 (2026-02-28)

- **RoGetActivationFactory: log all classes** - The Windows.Gaming.Input hook now logs every distinct (IID, activatableClassId) pair seen from RoGetActivationFactory once (e.g. "RoGetActivationFactory new pair: riid=... activatableClassId=..."). HSTRING is read via crash-safe HStringToNarrowSafe (SEH and WindowsGetStringRawBuffer from combase.dll) so invalid HSTRINGs do not cause AVs. Helps diagnose which WinRT classes games request.

---

## v0.12.154 (2026-02-28)

- **HID suppression: crash fix in CreateFile detours** - CreateFileA/CreateFileW detours no longer update HID device stats or call XInputWidget shared state when HID suppression is disabled. Previously the stats block ran for any HID device path, which could run before the XInput widget was initialized and cause a crash. The block is now gated with `ShouldSuppressHIDInput()` so it only runs when HID suppression is enabled.

---

## v0.12.153 (2026-02-28)

- **Removed global "Suppress Windows.Gaming.Input" setting** - The single global `suppress_windows_gaming_input` option was removed from Advanced tab and XInput widget. WGI suppression is now controlled only by the per-game-type options ("Suppress WGI for Unity games" / "Suppress WGI for non-Unity games") and by the hook suppression setting (Windows.Gaming.Input in Hook Stats / Advanced). WGI hook installation no longer checks the removed setting; only `HookSuppressionManager` and the per-game-type logic apply. Doc `RoGetActivationFactory_SpecialK_Comparison.md` updated accordingly.

---

## v0.12.152 (2026-02-28)

- **Active input APIs (Controller tab)** - Controller tab now shows "Active input APIs (last 10s)" derived from a single `InputActivityStats` class: XInput, DirectInput 8/7, Raw Input, HID, Windows.Gaming.Input, winmm joystick, and **GameInput (IGameInput)**. All existing input hooks update activity via `MarkActiveByHookIndex`; WGI and GameInput have dedicated hooks that call `MarkActive`. GameInput is detected by hooking `GameInputCreate` when the game loads **GameInput.dll** (Microsoft Game Input redist); LoadLibrary handling now installs GameInput hooks for `gameinput.dll` and WGI hooks only for `windows.gaming.input` modules.

---

## v0.12.151 (2026-02-28)

- **Hook statistics cleanup** - Removed duplicate `g_hid_api_stats`; HID hooks now use only `g_hook_stats` (Hook Stats tab). Kept `g_hid_device_stats` for HID device-type counts (DualSense/Xbox/generic). Display Settings hooks (ChangeDisplaySettings*, ShowWindow, SetWindowPos, SetWindowLong*) now update `g_hook_stats` so they appear in Hook Stats. Added `HookIndex hook_index` to `HookInfo` and a compile-time `static_assert` so `g_hook_info` array order stays aligned with the `HookIndex` enum.

---

## v0.12.150 (2026-02-28)

- **Controller tab added** - New Controller tab (XInput monitoring, remapping, HID devices, Battery Status, DualSense Input Report, etc.). Collapsible sections use indent/unindent at call site so the ImGui stack stays balanced and early returns cannot leave indent unmatched.

---

## v0.12.149 (2026-02-27)

- **Fix DX11 games not working with ReShade loaded** - DX11 games now work correctly when ReShade is loaded (addressing CreateDXGIFactory/CreateDXGIFactory1/CreateDXGIFactory2 hook resolution and DX11 device/swapchain creation with ReShade in the chain).

---

## v0.12.148 (2026-02-27)

- **D3D11 hooking in no-ReShade mode** - Added fps limiter support for DX11 in "no reshade mode".

---

## v0.12.147 (2026-02-27)

- **DXGI proxy: factory vtable hooking from CreateDXGIFactory1/2** - When a factory is returned from CreateDXGIFactory1 or CreateDXGIFactory2, the proxy now installs the IDXGIFactory::CreateSwapChain (vtable+10) hook on that factory before returning. Added `TryHookFactoryVtableFromPointer` and shared `InstallFactoryCreateSwapChainVtableHook`; hooking runs only when experimental features are enabled and skips if the vtable is already hooked.
- **DXGI proxy: IDXGIFactory4–7 includes** - Added includes for `<dxgi1_4.h>`, `<dxgi1_5.h>`, and `<dxgi1_6.h>` so `__uuidof(IDXGIFactory4)` through `IDXGIFactory7` are declared. Fixed CreateDXGIFactory2 call to pass all three arguments (Flags, riid, ppFactory).

---

## v0.12.146 (2026-02-27)

- **XInput stick mapping: 8 sliders + same-axes** - Stick mapping now uses four parameters per axis: Min Input, Max Input, Min Output (anti-deadzone), Max Output. Input range [min%, max%] is mapped to output [min%, max%] (e.g. 30%-70% → 10%-80%). Left and right stick each have 4 sliders when "Same for both axes" is on (default), or 8 sliders (4 for X, 4 for Y) when off. Radial and square processing modes both use the new mapping. Settings and curve view updated; config backward compatible with previous deadzone/sensitivity/min-output keys.

---

## v0.12.145 (2026-02-27)

- **Block WGI for Unity only** - Windows.Gaming.Input (WGI) factory suppression now applies only when the game is Unity. Unity is detected by the presence of UnityPlayer.dll only (same as Special K; no other Unity DLLs are checked). When "Suppress Windows.Gaming.Input" and "Continue rendering when unfocused" are on, RoGetActivationFactory requests for the three same IIDs as Special K (IGamepadStatics, IGamepadStatics2, IRawGameControllerStatics) are failed with E_NOTIMPL using hardcoded GUIDs, so Unity games (e.g. Hollow Knight) fall back to XInput. Non-Unity games are unaffected and keep full WGI support.

---

## v0.12.144 (2026-02-27)

- **XInput vibration test: auto-stop and timer** - Vibration test (Left/Right/Both motors) now auto-stops after 10 s. While running, the UI shows a countdown ("Stopping in X.X s"). The 10 s check runs whenever the XInput tab is visible; Stop button still ends vibration immediately.

---

## v0.12.143 (2026-02-27)

- **D3D9 prevent fullscreen: fix D3DERR_INVALIDCALL** - When forcing windowed mode (prevent fullscreen) for D3D9 CreateDevice, the addon now sets `FullScreen_RefreshRateInHz` to 0. Leaving it at the game's fullscreen refresh rate (e.g. 180) with `Windowed=1` caused D3DERR_INVALIDCALL (0x8876086C) with some drivers (e.g. NVIDIA). In windowed mode the refresh rate must be 0 per D3D9 semantics.

---

## v0.12.142 (2026-02-27)

- **D3D11 and DXGI error logging** - When D3D11CreateDevice or D3D11CreateDeviceAndSwapChain fails, the addon now logs `[D3D11 error] <api> returned 0x<hr>` so failures appear in the log. DXGI factory swapchain creation (CreateSwapChain, CreateSwapChainForHwnd, CreateSwapChainForCoreWindow, CreateSwapChainForComposition) in the factory wrapper now logs `[DXGI error] <method> returned 0x<hr>` on failure (up to 10 times per method to avoid spam).
- **DXGI factory swapchain hooks (CreateSwapChainForHwnd, CreateSwapChainForCoreWindow)** - Added vtable detours for IDXGIFactory1::CreateSwapChainForHwnd and CreateSwapChainForCoreWindow (indices 14 and 15) with the same error logging and swapchain hooking as the existing CreateSwapChain path. Implemented `HookFactory`: when a factory is created via CreateDXGIFactory2, the addon now hooks the factory vtable at CreateSwapChain (10), CreateSwapChainForHwnd (14), and CreateSwapChainForCoreWindow (15). New swapchains created via any of these paths are hooked and get the same DXGI error logging for subsequent calls.

---

## v0.12.141 (2026-02-27)

- **D3D9 CreateTexture experimental fix** - Experimental fix for CreateTexture (D3D9 pool upgrade / D3D9Ex path).

---

## v0.12.140 (2026-02-27)

- **D3D9 CreateDevice/CreateDeviceEx full-argument logging** - CreateDevice and CreateDeviceEx detours now log all arguments before any modifications, after present-parameter upgrades, and on success or failure. Logged: Adapter, DeviceType, hFocusWindow, BehaviorFlags, ppReturnedDeviceInterface; full D3DPRESENT_PARAMETERS (BackBufferWidth/Height/Format/Count, MultiSampleType/Quality, SwapEffect, hDeviceWindow, Windowed, EnableAutoDepthStencil, AutoDepthStencilFormat, Flags, FullScreen_RefreshRateInHz, PresentationInterval); for CreateDeviceEx also D3DDISPLAYMODEEX when non-null. Success/failure lines include hr and device pointer. Enables diagnosing D3D9 device creation failures (e.g. when creation appeared to hang after "Forcing windowed mode" with no outcome log).

---

## v0.12.139 (2026-02-27)

- **XInput hooks fix (e.g. Hollow Knight)** - XInput hooks were previously disabled by a stub in `InstallXInputHooks`; the stub was removed so hooks install correctly. Added a retry of `InstallXInputHooks(nullptr)` in `DoInitializationWithHwnd` so games that load XInput before the first present get hooked. Added "Suppress Windows.Gaming.Input (use XInput)" option in the XInput widget so games that use Windows.Gaming.Input can be forced to use XInput instead (with a warning when XInput is on but WGI is not suppressed). Exposed `IsXInputHooksInstalled()` for UI/status.

---

## v0.12.138 (2026-02-27)

- **DXGI error logging and re-enabled swapchain hooks** - All IDXGISwapChain / IDXGISwapChain1–4 and IDXGIOutput HRESULT-returning methods now log failures: each method logs up to 10 times per session via an internal static counter, using a shared `LogDxgiErrorUpTo10` helper and `[DXGI error] <method> returned 0x<hr>`. Re-enabled all vtable hooks in `HookSwapchain`: GetBuffer (9), GetDesc (12), GetFrameStatistics (16), GetLastPresentCount (17), GetDesc1/GetFullscreenDesc/GetHwnd/GetCoreWindow (18–21), IsTemporaryMonoSupported/GetRestrictToOutput/SetGetBackgroundColor/SetGetRotation (23–28), full IDXGISwapChain2 block (29–35), GetCurrentBackBufferIndex (36), SetHDRMetaData (40). Non-critical hook failures no longer abort hooking.

---

## v0.12.137 (2026-02-27)

- **D3D9 → D3D9Ex / flip swapchain upgrade in no-ReShade mode** - Full support for upgrading D3D9 games to D3D9Ex and FLIPEX when running without ReShade. Separate settings: "Enable D3D9 FLIPEX (with ReShade)" vs "Enable D3D9 FLIPEX (no-ReShade mode)" so ReShade and standalone paths can be configured independently. D3D9Ex resource creation: when the game passes D3DPOOL_MANAGED (1), the addon now passes the D3D9Ex managed pool (6) to CreateTexture/CreateVertexBuffer/CreateIndexBuffer/CreateVolumeTexture/CreateCubeTexture/CreateOffscreenPlainSurface so creation succeeds on IDirect3DDevice9Ex. Main tab no-ReShade section shows "Last D3D9 (no-ReShade)" state (CreateDevice vs CreateDeviceEx, swap effect, back buffer count, sync interval, windowed/fullscreen). PresentEx hook uses the appropriate FLIPEX setting based on whether ReShade is loaded.

---

## v0.12.136 (2026-02-27)

- **VSync & Tearing: Reshade vs no-ReShade UI** - Main tab "VSync & Tearing" checkboxes are split into two code paths. `DrawDisplaySettings_VSyncAndTearing_Checkboxes` was renamed to `DrawDisplaySettings_VSyncAndTearing_Checkboxes_Reshade` (used when ReShade is loaded). New `DrawDisplaySettings_VSyncAndTearing_Checkboxes_NoReshadeMode` is used when running without ReShade (e.g. D3D9 FLIPEX from CreateDevice upgrade): same options (Force VSync ON/OFF, Prevent Tearing, Increase Backbuffer to 3, Enable Flip Chain, Enable Flip State) are shown; DXGI vs D3D9 visibility is derived from present traffic and D3D9 present hooks instead of ReShade swapchain/device API.

---

## v0.12.135 (2026-02-27)

- **D3D9 FPS limiter in no-ReShade mode** - When the addon is used without ReShade (e.g. standalone or proxy d3d9), D3D9 device creation is now hooked via Direct3DCreate9/Direct3DCreate9Ex and IDirect3D9::CreateDevice / IDirect3D9Ex::CreateDeviceEx. The first created device gets Present/PresentEx and vtable hooks, so the FPS limiter works for D3D9 games even when ReShade is not loaded. The FPS counter does not work in this mode (it relies on ReShade swapchain/overlay); only the limiter is active.

---

## v0.12.134 (2026-02-27)

- **VRAM info: create DXGI factory and adapter once** - `GetVramInfo` (used by Main tab and overlay for VRAM used/total) no longer calls `CreateDXGIFactory1` and `EnumAdapters1` every frame. It now uses the shared DXGI factory and caches the first adapter (IDXGIAdapter3); only `QueryVideoMemoryInfo` runs per query. Cache is thread-safe (SRWLOCK) and is cleared on query failure (e.g. device removed) so the next call re-creates the adapter.

---

## v0.12.133 (2026-02-27)

- **D3D9 device error logging (59 methods)** - Hook all HRESULT-returning IDirect3DDevice9/IDirect3DDevice9Ex methods and log every failure; on first failure per method also log full arguments and HRESULT name (e.g. D3DERR_INVALIDCALL). Covers Reset, BeginScene, EndScene, Clear, TestCooperativeLevel, CreateAdditionalSwapChain, GetBackBuffer, GetSwapChain, all Create* (texture, VB, IB, render target, depth stencil, offscreen surface, state block, vertex declaration, vertex/pixel shader, query), UpdateSurface, UpdateTexture, GetRenderTargetData, GetFrontBufferData, StretchRect, ColorFill, SetRenderTarget, GetRenderTarget, SetDepthStencilSurface, GetDepthStencilSurface, CreateStateBlock, BeginStateBlock, EndStateBlock, SetStreamSource, SetIndices, SetVertexDeclaration, SetFVF, SetStreamSourceFreq, DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP, ProcessVertices, SetViewport, SetTransform, SetRenderState, GetTexture, SetTexture, SetVertexShader, SetPixelShader, and Device9Ex: CheckDeviceState, CreateRenderTargetEx, CreateOffscreenPlainSurfaceEx, CreateDepthStencilSurfaceEx, ResetEx, GetDisplayModeEx. Plan and optional future methods documented in `docs/tasks/d3d9_error_logging_plan.md`.
- **D3D9 vtable install log** - `InstallD3D9DeviceVtableLogging` now logs the device pointer when installing hooks (after the "already installed" check).

---

## v0.12.132 (2026-02-27)

- **D3D9 CreateTexture error diagnostics** - When `IDirect3DDevice9::CreateTexture` fails, the first failure in a session now logs full arguments (device, Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle) and the HRESULT name (e.g. D3DERR_INVALIDCALL for 0x8876086C). Later failures still log only the short error line to avoid log spam.

---

## v0.12.131 (2026-02-27)

- **Log level filter** - When the logging level is set to "Warning" (or "Error Only"), INFO and Debug lines were still written because some code called the logger directly (`display_commander::logger::LogInfo`, `LogInfoDirectSynchronized`, or `DisplayCommanderLogger::Log`) and bypassed the level check in `utils/logging.cpp`. The minimum log level is now enforced inside the logger: `DisplayCommanderLogger::Log()` and the namespace helpers (`LogInfo`, `LogWarning`, `LogDebug`, `LogInfoDirectSynchronized`) all respect `g_min_log_level`, so only messages at or above the selected severity are written.

---

## v0.12.130 (2026-02-27)

- **Fix DX9 UI crash in Main tab (VSync & Tearing)** - Crash occurred in the present-mode line when calling Discord overlay visibility check (`IsWindowWithTitleVisible`) on the D3D9 path. Discord overlay check and warning are now skipped when the API is D3D9 so the UI no longer crashes or hangs when the overlay is open with a D3D9 game.
- **VSync & Tearing tooltip and checkboxes** - Tooltip context now keeps the swapchain desc alive (`desc_holder`) to avoid use-after-free if the desc is updated while the tooltip is open. "Enable Flip Chain" / `is_flip` is computed only for DXGI APIs (not D3D9) to avoid comparing D3D9 present_mode to DXGI constants.
- **Main tab crash diagnosis** - Added `RECORD_DETOUR_CALL` at entry of all main-tab draw functions (DrawMainNewTab, DrawDisplaySettings, DrawDisplaySettings_*, DrawAudioSettings, DrawWindowControls, DrawImportantInfo, frame graphs, overlay, etc.) and granular points inside `DrawDisplaySettings_VSyncAndTearing_PresentModeLine` so crash reports show the last reached call site (function:line).

---

## v0.12.129 (2026-02-27)

- **D3D9 vtable indices** - Fixed IDirect3DDevice9 vtable indices to match d3d9.h / ReShade declaration order. CreateTexture..CreateDepthStencilSurface are 23-29 (was 21-27); CreateOffscreenPlainSurface is 36 (was 28). Added `d3d9_vtable_indices.hpp` with a full `VTable` enum for all IDirect3DDevice9/IDirect3DDevice9Ex slots; Present and PresentEx hooks now use `VTable::Present` and `VTable::PresentEx` instead of magic 17 and 121.
- **D3D9 detour first-call logging** - Each D3D9 device vtable detour (CreateTexture, CreateVolumeTexture, CreateCubeTexture, CreateVertexBuffer, CreateIndexBuffer, CreateOffscreenPlainSurface, CreateRenderTarget, CreateDepthStencilSurface) logs once on first call: `[D3D9] First call: IDirect3DDevice9::<Method>`.

---

## v0.12.128 (2026-02-27)

- **DXGI flip state removal** - Removed `GetIndependentFlipState`, `DxgiBypassMode`, and `GetFlipStateForAPI`. DXGI flip state is no longer queried or shown in the main tab, swapchain tab, overlay, or Reflex section. PresentMon still reports flip state from ETW using a simpler `PresentMonFlipMode` enum (Unset, Composed, Overlay, IndependentFlip, Unknown) in the PresentMon ETW subsection only.

---

## v0.12.127 (2026-02-27)

- **Fix D3D9 device vtable logging crash** - Install D3D9 device vtable logging (CreateTexture, CreateRenderTarget, etc.) only when Present hooks were installed successfully (i.e. when the device is IDirect3DDevice9Ex). On base IDirect3DDevice9 the device from ReShade's get_native() can be a proxy whose vtable layout may not match, causing ACCESS_VIOLATION in the game (e.g. Assassin's Creed Brotherhood). Vtable logging is now skipped for non-Ex D3D9 devices.

---

## v0.12.126 (2026-02-27)

- **Fix controller support in Death Stranding** - Default for "Suppress Windows.Gaming.Input" (Advanced tab) changed to off; setting key renamed to SuppressWindowsGamingInput2 so existing configs pick up the new default. Restores controller input when using the addon with Death Stranding.

---

## v0.12.125 (2026-02-27)

- **OpenGL FPS limiter** - Use **wglSwapBuffers** as an FPS limiter call site (same pattern as D3D9 Present). Added `FpsLimiterCallSite::opengl_swapbuffers`; when chosen, custom FPS limiter and frame-time recording run from the OpenGL present path (ChooseFpsLimiter, OnPresentFlags2, RecordNativeFrameTime, RecordFrameTime, HandlePresentAfter). OpenGL games can use the same FPS limiter as D3D9/DXGI with automatic source selection.

---

## v0.12.124 (2026-02-27)

- **D3D9 device vtable logging** - Hook all IDirect3DDevice9 resource-creation methods that ReShade wraps: CreateTexture, CreateVolumeTexture, CreateCubeTexture, CreateVertexBuffer, CreateIndexBuffer (in addition to existing CreateRenderTarget, CreateDepthStencilSurface, CreateOffscreenPlainSurface). Each detour uses RECORD_DETOUR_CALL and logs on FAILED(hr).
- **D3D9 Active APIs in Main tab** - When IDirect3DDevice9::Present or PresentEx is called, D3D9 now appears under "Active APIs" in the Main tab (present traffic timestamp was already updated for Present; PresentEx detour now updates it as well).

---

## v0.12.123 (2026-02-27)

- **OpenGL and PCLStats ETW hooks take HMODULE** - `InstallOpenGLHooks` and `InstallPCLStatsEtwHooks` now take an `HMODULE` argument (the loaded module). Call sites in the LoadLibrary/OnModuleLoaded path pass the module handle so hooks are installed on the correct DLL when the addon is used as a proxy (e.g. opengl32 or advapi32). Aligns with the existing DirectInput/DirectInput8/DbgHelp pattern.

---

## v0.12.122 (2026-02-27)

- **D3D9 proxy (Wine d3d9.spec parity)** - Added missing d3d9.dll exports: `Direct3DShaderValidatorCreate9` and `DebugSetMute`, with forwarding to the real system d3d9.dll. Fixed exports.def typo (`EXPORTS` was written as `` `XPORTS ``), which caused linker failures when building the addon as a proxy DLL.

---

## v0.12.121 (2026-02-27)

- **Background FPS** - Default background FPS limit changed from 30 to 60. Added "Background FPS" checkbox (default off): when off, the game uses the same FPS limit in background as in foreground; when on, the background limit slider applies. Checkbox and limit slider are on the same line; slider is greyed out when the checkbox is off.

---

## v0.12.120 (2026-02-27)

- **Frame timeline (Reflex path)** - Added placeholder UI message when native Reflex is active: "Frame timeline: not implemented yet for Reflex path." in the performance overlay section.

---

## v0.12.119 (2026-02-27)

- **Crash handler refactor** - Removed duplication between `VectoredExceptionHandler` and `UnhandledExceptionHandler`. Shared logic moved into `LogCrashReport(PEXCEPTION_POINTERS, header_line, log_section_context)` and `WriteMultiLineToDebugLog`; both handlers now call `LogCrashReport` with the appropriate header and section-context flag. Unhandled report now also logs system memory load (aligned with VEH).

---

## v0.12.118 (2026-02-27)

- **Crash/exit handler logging** - Crash and vectored exception handler paths now use `LogInfoDirectSynchronized` instead of `LogInfo`. `WriteToDebugLog` and the single exit message in `OnHandleExit` write directly to the log file (synchronized, with flush), so crash reports are not lost if the logger's writer thread is blocked or the process is in a bad state.

---

## v0.12.117 (2026-02-26)

- **Inject Reflex checkbox (Main tab)** - Added "Inject Reflex" checkbox on the Main tab (default off), shown only when native Reflex is not active. When enabled, the addon injects Reflex (sleep + latency markers) for games without native Reflex support.

---

## v0.12.116 (2026-02-26)

- **DirectInput hooks split by DLL** - DirectInput install is now split into `InstallDirectInput8Hooks(HMODULE)` (dinput8.dll) and `InstallDirectInputHooks(HMODULE)` (dinput.dll). Both are called from `OnModuleLoaded` with the loaded module handle as argument, so the correct module is hooked without `GetModuleHandle` lookup.
- **Separate HookType for DirectInput 8** - Added `HookType::DINPUT8` for dinput8.dll (DirectInput8Create) alongside existing `HookType::DINPUT` for dinput.dll (DirectInputCreateA/W). Hook suppression settings now have separate `DInput8Hooks` / `SuppressDInput8Hooks` / `DInput8HooksInstalled` in `DisplayCommander.HookSuppression` and `DisplayCommander.HooksInstalled`.

---

## v0.12.115 (2026-02-26)

- **DirectInput and OpenGL hooks via OnModuleLoaded** - Fixed creating hooks when Display Commnader is used as dll proxy.
- **PCLStats ETW hooks via OnModuleLoaded** - Fixed creating hooks when Display Commander is used as dll proxy.

---

## v0.12.114 (2026-02-26)

- **Suppress Windows.Gaming.Input UI** - "Suppress Windows.Gaming.Input" checkbox moved from Advanced tab to Main tab (Window Control), shown directly below "Continue Rendering in Background". Checkbox is only visible when Windows.Gaming.Input hooks are installed for the current process (`g_windows_gaming_input` / WGI hooks active). Added global atomic `g_windows_gaming_input` (set on hook install/uninstall and when a suppressible WGI factory request is seen).

---

## v0.12.113 (2026-02-26)

- **Suppress Windows.Gaming.Input (Special K–aligned)** - "Suppress Windows.Gaming.Input" now only fails the same three RoGetActivationFactory requests that Special K suppresses (IGamepadStatics, IGamepadStatics2, IRawGameControllerStatics). Other Windows.Gaming.Input interfaces (racing wheel, arcade stick, flight stick, etc.) are passed through so they keep working when the option is enabled.

---

## v0.12.112 (2026-02-26)

- **Reflex RestoreSleepMode crash fix** - Fixed ACCESS_VIOLATION (0xC0000005) in dxgi when using "Game Defaults" Reflex mode. `RestoreSleepMode` was calling NVAPI with a null device when the game had never called `NvAPI_D3D_SetSleepMode`; it now returns early if the device is null and uses a default params struct when params were not stored. `NvAPI_D3D_SetSleepMode_Direct` now rejects null device or params and returns `NVAPI_INVALID_ARGUMENT` instead of calling into the driver.
- **PDB copied next to addon DLL** - CMake post-build step copies the addon PDB into the same directory as the DLL so debug symbols are available for crash dumps when deploying to ReShade.

---

## v0.12.111 (2026-02-26)

- **Stuck-report direct logging (relative time only)** - The stuck-methods / undestroyed-guards report now writes to the log file directly from the same thread that runs `CheckStuckMethodsAndLogUndestroyedGuards` via `LogInfoDirectSynchronized`, so output is visible even if the logger's writer thread is blocked. Timestamps use relative time only (`t+XX.Xs`, `X.Xs ago`) from `get_now_ns()` with no `FileTimeToLocalFileTime` or `FileTimeToSystemTime` system calls. Added `g_global_frame_id_last_updated_ns` and `g_last_window_message_processed_ns` (set alongside existing filetime globals) so the report shows "last_updated=X.Xs ago" and "last Windows message processed: X.Xs ago" without wall-clock conversion.

---

## v0.12.110 (2026-02-26)

- **LoadLibrary hooks: g_module_srwlock never held during system calls** - Module tracking now uses two helpers: `FillModuleInfoFromHandle` (system calls only, no lock) and `TryAddModuleUnderLock` (lock only, no system calls). All six detours (LoadLibraryA/W, LoadLibraryExA/W, LoadPackagedLibrary, LdrLoadDll) call the original API first, then fill module info without the lock, then update shared state under the lock only. This avoids holding `g_module_srwlock` across GetModuleFileNameW, GetModuleInformation, or GetModuleFileTime.

---

## v0.12.109 (2026-02-26)

- **Stuck methods / undestroyed guards report** - The periodic stuck-methods and undestroyed-detour-guards diagnostic now flushes logs so the output is visible in ReShade logs.

---

## v0.12.108 (2026-02-26)

- **FPS limit change in logs** - FPS limiter changes are now logged so users can see when the limit is applied or changed in ReShade logs.

---

## v0.12.107 (2026-02-26)

- **DXGI Present re-entrancy guard** - Added an atomic re-entrancy guard in the DXGI Present detours so that when the detour is already executing (e.g. Present1 calling back into Present), the inner call bypasses addon logic and calls the original directly, avoiding recursion or crashes. Present1 increments the guard before calling the original and decrements on all return paths (including when the original pointer is null).

---

## v0.12.106 (2026-02-26)

- **Added "FPS Limiter Safe mode"** - Added safe mode to fps limiter in case other modes are broken.

---

## v0.12.105 (2026-02-25)

- **Active APIs (Main tab)** - Main tab now shows "Active APIs" next to "APIs (loaded by host)": a list of graphics APIs that had present/swap traffic in the last 1 second (DXGI, D3D9, or OpenGL32). Based on calls to our present detours (IDXGISwapChain::Present, IDirect3DDevice9::Present, wglSwapBuffers). Uses lock-free timestamp tracking in `present_traffic_tracking`; tooltip explains the meaning.

## v0.12.104 (2026-02-25)

- **Removed Force FG Auto (Streamline)** - Removed the "Force FG Auto (Streamline)" option from Main → Display Settings (Misc). The slDLSSGSetOptions hook and related setting have been removed; NGX parameter updates from DLSS-G still occur via slDLSSGGetState.

## v0.12.103 (2026-02-25)

- **APIs (loaded by host)** - Main tab now shows which graphics/API libraries the game (or host process) loaded via LoadLibrary, e.g. "dxgi d3d11" or "opengl32". Uses GetCallingDLL so loads from Display Commander or ReShade are excluded. Tracks dxgi, d3d9/10/11/12, ddraw, opengl32, vulkan-1, libEGL, libGLESv2.
- **LoadLibrary host-API tracking deadlock fix** - EnumerateLoadedModules no longer holds g_module_srwlock while taking g_host_loaded_apis_srwlock; host-API names are recorded after releasing the module lock to avoid deadlock with UI or other threads.

## v0.12.102 (2026-02-25)

- **Added scripts for SK-like global injection** - Added `scripts/dc_service/`: `download_dc.bat` (downloads latest_build addon .addon64/.addon32 to the script folder, copies as dc_installer64.dll for SetupDC), and `dc_start32.cmd` / `dc_start64.cmd` / `dc_stop32.cmd` / `dc_stop64.cmd` for service control.

## v0.12.101 (2026-02-25)

- **Generic dll proxy generator from Wine .spec** - Added `scripts/gen_proxy_from_spec.py`: takes DLL name and path to a Wine .spec file, outputs `xxx_proxy.cpp` and `xxx_exports.def` (fragment to paste into proxy_dll). Supports ordinal block, `@ stdcall` / `@ stub`, winmm (winmmHooked + init), d3d11 (gdi32 fallback). Winmm proxy and exports.def winmm section are now generated from `scripts/specs/winmm.spec`; exports aligned with Wine (comment-only symbols in spec are not exported).

## v0.12.100 (2026-02-25)

- **winmm.dll proxy: ordinal exports** - Added Wine-style ordinal exports: ordinal 2 = PlaySoundA (WINMM_2), ordinals 3/4 = WINMM_3/WINMM_4 stubs (forward to real winmm or return 0). Matches [Wine winmm.spec](https://github.com/wine-mirror/wine/blob/master/dlls/winmm/winmm.spec).

## v0.12.99 (2026-02-25)

- **winmm.dll proxy: added missing APIs** - Implemented the remaining winmm exports for parity with system winmm (Wine API reference): GetDriverFlags, OpenDriverA, DrvClose, DrvDefDriverProc, DrvOpen, DrvOpenA, DrvSendMessage, mmioInstallIOProc16. All forward to the real winmm.dll (or winmmHooked.dll when present).

## v0.12.98 (2026-02-25)

- **DXGI factory hooks** - Added CreateDXGIFactory2 detour and hook installation (same pattern as CreateDXGIFactory/CreateDXGIFactory1). DXGI factory hooks are disabled for now (behind experimental / hook suppression).

## v0.12.97 (2026-02-25)

- **Supports loading Display Commander through winmm.dll**

## v0.12.96 (2026-02-25)

- **Added loading Display Commander as opengl32.dll (untested)**

## v0.12.95 (2026-02-25)

- **Version resource FileDescription shows bitness** - The addon DLL's version resource FileDescription now includes "(32-bit)" or "(64-bit)" so users can tell at a glance which build they have (e.g. in Explorer file properties).

## v0.12.94 (2026-02-25)

- **Add ability to use Display Commander as d3d9.dll proxy (untested)** - Proxy DLL can be used as d3d9.dll for Direct3D 9 games; forwards all D3D9 exports to system d3d9.dll (same pattern as ddraw/dxgi). Untested.

## v0.12.93 (2026-02-25)

- **Add ability to use Display Commander as ddraw.dll proxy** - Proxy DLL can be used as ddraw.dll for DirectDraw games; forwards to system ddraw.dll (same pattern as dxgi/d3d11/d3d12).

## v0.12.92 (2026-02-25)

- **NVIDIA profile search simplification** - Profile lookup for the current exe now uses a single path: `NvAPI_DRS_FindApplicationByName` with the full exe path (no profile enumeration). Find, create, delete, get details, and set setting all go through this. Removed `NvAPI_DRS_FindProfileByName` and profile/app enumeration for finding profiles; `SetOrDeleteProfileSettingForExe` (e.g. rundll32) supports only the current process exe. Cache and refresh behavior unchanged; UI still shows at most one matching profile.

## v0.12.90 (2026-02-24)

- **Independent window hotkey** - Optional hotkey to open/close the independent (standalone) settings window. Configurable in the Hotkeys tab as "Independent window toggle"; no default binding (empty = disabled). Toggles window visibility; does not persist a separate state.
- **Independent window hotkey fix** - Hotkeys are now processed when the ReShade overlay is open or when the independent window has focus (not only when the game is in foreground), so the independent-window toggle can close the window from the standalone window.
- **Move to secondary monitor hotkey** - New hotkey "Move to secondary monitor" (default `numpad-`) sets the target display to the first non-primary monitor. Only when game is in foreground; pairs with "Move to primary monitor" (`numpad+`).
- **Hotkeys tab** - Hotkey definition count centralized as `kHotkeyDefinitionCount` (22) in `hotkeys_tab.hpp`; load/sync checks use it instead of magic numbers.

## v0.12.89 (2026-02-24)

- **Hotkeys tab / ImGui wrapper** - `DrawHotkeysTab` now takes `IImGuiWrapper&` so the Hotkeys tab can be drawn in both the ReShade overlay and the independent (standalone) UI. Enables adding the Hotkeys tab to the independent settings window when desired.

## v0.12.88 (2026-02-24)

- **Vulkan tab / ImGui wrapper** - `DrawVulkanTab` now takes `IImGuiWrapper&` so the Vulkan (experimental) tab can be drawn in both the ReShade overlay and the independent (standalone) UI. Added a **Vulkan (Experimental)** tab to the independent settings window.

## v0.12.87 (2026-02-24)

- **Performance tab / ImGui wrapper** - `DrawPerformanceTab` now takes `IImGuiWrapper&` so the tab can be drawn in both the ReShade overlay and the independent (standalone) UI. Added a **Performance** tab to the independent settings window (alongside Main, NVIDIA Profile, Advanced, Performance Overlay).

## v0.12.86 (2026-02-24)

- **Experimental: flip swapchain upgrade crash prevention** - Added experimental support to prevent crashes when upgrading to a flip-model swap chain. Not enabled by default; enable in settings when ready to test.

## v0.12.85 (2026-02-24)

- **NVIDIA profile match score** - Match score is now +1000 per non-empty app-entry field (app name, file-in-folder, user-friendly name, launcher, command line, is_metro, is_command_line) and +1 per character in string fields. More specific and longer identifiers rank higher when sorting matching profiles.

## v0.12.84 (2026-02-24)

- **NVIDIA profile search** - Before enumerating all profiles, the addon now tries `NvAPI_DRS_FindApplicationByName` with the full exe path. If the driver returns one profile, that profile is used first and its settings are loaded; enumeration then adds any other matching profiles without duplicating the one already found by path.
- **NVIDIA profile match score and sorting** - Each matching profile now has a score (number of non-empty app-entry fields: app name, file-in-folder, user-friendly name, launcher, command line, is_metro, is_command_line). Profiles are sorted by score descending (more specific matches first). The "Matching profile(s)" list tooltip shows the match score.

## v0.12.83 (2026-02-24)

- **Prevent display sleep & screensaver** - Renamed screensaver-related UI and tooltips to "Prevent display sleep & screensaver". Main tab combo label, Advanced tab checkbox, and tooltips now use this label; note in tooltip points users to enable the option in the Advanced tab for the mode to take effect.

## v0.12.82 (2026-02-24)

- **Manual color space combo** - Advanced tab manual color space dropdown now lists all DXGI color space types (23 options) with bracket labels (sRGB, scRGB, HDR10, etc.). Dropdown is taller (~14 visible rows) and tooltip notes that the list is scrollable.

## v0.12.81 (2026-02-24)

- **Color space** - Manual color space (Advanced tab) and auto format-based color space are applied in the present path; when manual is not "No changes", it overrides auto.

## v0.12.80 (2026-02-24)

- **NVIDIA profile matching** - Profile matching is now path-aware and uses the "File in folder" rule. When a profile stores a full application path (e.g. `...\KingdomComeDeliverance\Bin\Win64\KingdomCome.exe`), only that path or paths under it match, so games with the same exe name in different folders (e.g. KCD1 vs KCD2) no longer share the wrong profile. When a profile has "File in folder" set, the match requires at least one of those files to exist in the current process directory.
- **NVIDIA Profile tab tooltip** - Hovering a row in the "Matching profile(s)" list shows a tooltip with the full profile application entry: App (exe), User-friendly name, Launcher, File in folder, Metro/UWP, and Command line when set.

## v0.12.79 (2026-02-24)

- **DXGI swapchain wrapper** - When Display Commander is loaded as a DXGI proxy DLL, swap chains created via the wrapped factory are now wrapped in `DXGISwapChain4Wrapper`. The wrapper forwards all IDXGISwapChain4 methods to the real swap chain and fixes a use-after-free by having `DXGIFactoryWrapper` AddRef the original factory in its constructor so the real factory stays alive after the proxy releases its reference.

## v0.12.78 (2026-02-24)

- **Manual color space / DXGI wrapper** - When Display Commander is loaded as dxgi.dll or as a proxy DLL, the DXGI swap chain wrapper now overrides `SetColorSpace1` so that the Advanced-tab manual color space selection (No changes, sRGB, scRGB, HDR10 ST2084, HDR10 HLG) is applied. The selected option is mapped to the corresponding `DXGI_COLOR_SPACE_TYPE` (e.g. sRGB → G22_P709, scRGB → G10_P709, HDR10 ST2084 → G2084_P2020, HDR10 HLG → YCBCR_STUDIO_G2084_P2020). Only active when DC is in the DXGI load path; ReShade overlay color space logic is unchanged.

## v0.12.77 (2026-02-24)

- **Loading** - Fixed Doom Dark Ages not loading.

## v0.12.76 (2026-02-24)

- **Performance Overlay / independent UI** - Added a **Performance Overlay** tab to the independent (standalone) settings window.
- **Independent window** - Fixed interacting with the independent window when clip_cursor is enabled.

## v0.12.75 (2026-02-24)

- **Main tab** - Added "Show independent window" checkbox (when running in ReShade). Check to open the standalone settings window (Main, Profile, Advanced) in a separate window; uncheck to close it. Only visible in the ReShade overlay, not in the standalone UI.

## v0.12.74 (2026-02-24)

- **Manual color space** - Default set to "No changes" (0); first option label renamed from "Unknown" to "No changes". Manual color space is now a `ManualColorSpace` enum (NoChanges, sRGB, scRGB, HDR10_ST2084, HDR10_HLG) with `GetManualColorSpace()`/`SetManualColorSpace()`; Advanced tab combo and swapchain color-space logic use the enum. Config still stores 0–4.

## v0.12.73 (2026-02-24)

- **Standalone UI (independent window)** - Switched the installer and "Settings (No ReShade)" window from DirectX 9 to Win32 + OpenGL (WGL).

## v0.12.72 (2026-02-24)

- **Standalone UI (Main tab)** - Further fixes for crashes (0xC0000005) in standalone/installer UI: Resolution widget now takes `IImGuiWrapper&` and uses it for all ImGui calls (no direct `ImGui::*`). Settings wrapper uses `imgui->GetIO()` when the wrapper is passed so slider logic never touches the wrong ImGui context. `DrawDisplaySettings` now takes `GraphicsApi` instead of `effect_runtime*`; when API is `Unknown` (standalone) it skips API-dependent sections without dereferencing a runtime. Wrapper gained `InputInt`, `Button(label, size)`, `GetDisplaySize`, `GetIO`, `SetNextWindowPos`, `SetNextWindowSize`.

## v0.12.71 (2026-02-24)

- **ImGui wrapper** - `GetWindowDrawList()` now returns a proxy object (`IImDrawList*`) instead of raw `ImDrawList*`. Avoids crashes when the addon and runtime use different ImGui/ImDrawList layouts (linker using wrong object). Main tab draw-list call sites (audio VU bars, etc.) use the proxy; ReShade and standalone wrappers each provide an implementation that forwards to their ImGui draw list.

## v0.12.70 (2026-02-24)

- **Window management** - Added 21.5:9 aspect ratio option to the Borderless Windowed (Aspect Ratio) dropdown (between 21:9 and 32:9).

## v0.12.69 (2026-02-24)

- **Loading** - Add loading libraries through .dc64/.dc32/.dc/.asi (in addition to .dc64/.dc32, now also load .dc and .asi from the addon directory).

## v0.12.68 (2026-02-24)

- **Standalone UI (Main tab)** - Fixed crashes (0xC0000005) when opening the Main tab in the standalone/installer UI. All UI color helpers (nested headers, selected button, icon) now use the ImGui wrapper when available: added `ui::colors::*` overloads taking `IImGuiWrapper*` in `res/ui_colors.hpp`. When runtime is null (standalone), the Frame Time Graph section in Important Info is skipped and shows "Frame timing and graphs are only available when running in-game." instead of calling table/draw-list code. Audio VU bars (per-channel, overlay, and main strip) now null-check `GetWindowDrawList()` before calling `AddRectFilled`/`AddRect` so standalone does not crash when the draw list is unavailable.

## v0.12.67 (2026-02-24)

- **Color space selector** - Added a color space dropdown below "Auto color space" in Advanced → HDR and Display Settings. When Auto color space is off, the selected space (Unknown, sRGB, scRGB, HDR10 ST2084, HDR10 HLG) is applied to the swap chain on present. When Auto is on, format-based behavior is unchanged.

## v0.12.66 (2026-02-24)

- **ImGui wrapper / UI colors** - Switched wrapper to use `ImVec4`/`ImVec2` only; removed `wrapper_colors` and custom wrapper color/vec types. Shared UI (Advanced tab, Nvidia Profile tab) now uses `ui::colors::*` from `res/ui_colors.hpp` directly (e.g. `ui::colors::TEXT_WARNING`, `ICON_SUCCESS`). Standalone wrapper matches the same interface.

## v0.12.65 (2026-02-24)

- **Advanced tab / standalone UI** - Migrated Advanced tab to standalone UI. The Advanced tab is now available in both the No-ReShade settings window and the installer (SetupDC) window as an "Advanced" tab alongside Settings, Profile, Setup, and Games.

## v0.12.64 (2026-02-24)

- **Main tab / standalone UI** - Migrated CPU Control from the main tab to a shared implementation using the ImGui wrapper (same pattern as the Nvidia Profile tab). CPU Control now works in both the ReShade overlay and the standalone UI. Added a **Settings** tab to the standalone installer UI and to the No-ReShade settings window; the tab shows the shared CPU Control (core affinity slider and status). Wrapper gained `Indent`, `Unindent`, and `SliderInt`; standalone settings bridge gained `GetCpuCoreCount`, `GetCpuCores`, and `SetCpuCores`.

## v0.12.63 (2026-02-24)

- **Nvidia Profile tab / ImGui wrapper** - Migrated the Nvidia Profile (Inspector) tab to use an ImGui abstraction so it can run in both the ReShade overlay and the standalone UI. Introduced a common base (`IImGuiWrapper`), a ReShade-backed wrapper (header-only), and a standalone-backed wrapper; the shared tab takes API version (dx11, dx12, etc.) and the wrapper. Added a **Profile** tab to the standalone installer UI (SetupDC) and to the No-ReShade settings window as the second tab.

## v0.12.62 (2026-02-24)

- **Standalone UI without ReShade** - Added support for running the standalone settings UI without ReShade. Activate by creating a `.NO_RESHADE` file in the game directory.

## v0.12.61 (2026-02-24)

- **Run without ReShade** - Code refactor to allow running Display Commander without ReShade in the future.

## v0.12.60 (2026-02-23)

- **FPS limiter** - Simplified FPS limiter logic for frame generation.

## v0.12.59 (2026-02-23)

- **PCLStats reporting** - Fixed and re-enabled PCL stats reporting for injected reflex.

## v0.12.58 (2026-02-23)

- **ADHD on game display** - Fixed bug where the background window was still created/shown when the game was fullscreen and already covered the monitor. The overlay is now hidden when the game window covers 100% of its monitor (fullscreen); it is shown only when the game is windowed or does not cover the screen.

## v0.12.57 (2026-02-23)

- **Stuck detection** - When no new frame is generated for 15s, the log now also prints the last time a Windows message was processed (game window WndProc). Helps distinguish a stuck message queue (no messages dispatched → old timestamp) from other causes of frame stall.

## v0.12.56 (2026-02-23)

- **ADHD Multi-Monitor** - Tooltip for "ADHD Multi-Monitor Mode" now shows background window debug info on hover: HWND (and null/not null), window position, size, and visibility. Added thread-safe `GetBackgroundWindowDebugInfo` API and made `background_hwnd_` atomic for safe reads from the UI thread.

## v0.12.55 (2026-02-23)

- **Reflex FPS limiter** - Implemented `ShouldUseReflexAsFpsLimiter()`: Reflex `minimumIntervalUs` (frame-rate limit) is now applied only when FPS limiter mode is Reflex. Other modes (OnPresentSync, Disabled, LatentSync) keep Reflex low-latency/boost but do not use Reflex for FPS limiting. Used in NVAPI Reflex manager, NvLowLatencyVk SetSleepMode, and Vulkan loader SetSleepMode paths.

## v0.12.54 (2026-02-23)

- **ADHD Multi-Monitor** - Fixed crashes caused by not processing messages for the ADHD overlay window. The background window is now created and destroyed on the message-pump thread so that thread owns the window and receives its messages; position/show requests are published from the monitoring thread and applied on the pump thread.

## v0.12.53 (2026-02-23)

- **ADHD Multi-Monitor** - Made ADHD multi-monitor feature more reliable (V2).

## v0.12.52 (2026-02-23)

- **ADHD Multi-Monitor** - Background overlay window is now created hidden; shown only when ADHD is enabled and game is in foreground (PositionBackgroundWindow).

## v0.12.51 (2026-02-23)

- **ADHD Multi-Monitor** - Initialize and SetEnabled are now delayed until frame 500 (ReShade present count). Avoids early-present issues by not running ADHD init during the first 500 frames.

## v0.12.50 (2026-02-23)

- **DLSS Render Profile override (Main tab)** - When a driver profile has a DLSS Render Profile override (DLSS-SR or DLSS-RR preset not default), a "Clear" button is shown. Clicking it sets both DLSS-SR and DLSS-RR preset to default on the first matching NVIDIA profile and refreshes the status.

## v0.12.49 (2026-02-23)

- **DLSS Information (Main tab)** - Show override status for DLSS Render Profile: whether a driver profile exists for this game, profile name(s), and for DLSS-SR and DLSS-RR preset the current driver value (Not set, Preset A–O, Latest). A warning is shown when the profile overrides the default. Uses the same cached data as the NVIDIA Profile tab.

## v0.12.48 (2026-02-22)

- **DLSS** - Added warning when NVIDIA App override is used.

## v0.12.47 (2026-02-22)

- **DLSS module tracking** - Display Commander detects DLSS version when using Nvidia App override (tracks .bin and DLL loads; shows paths in Main tab → DLSS Information).

## v0.12.46 (2026-02-22)

- **ReShade base path** - Before loading Reshade64.dll/Reshade32.dll, Display Commander now sets `RESHADE_BASE_PATH_OVERRIDE` to the same folder used for DisplayCommander.toml (game exe directory). ReShade.ini and related files are therefore stored next to the game, alongside DisplayCommander.toml.

## v0.12.45 (2026-02-22)

- **Main tab** - Config and log file buttons: "Config" opens DisplayCommander.toml, "Log" opens DisplayCommander.log, plus buttons for reshade.log and reshade.ini. All show full path in tooltip on hover.

## v0.12.44 (2026-02-22)

- **Main tab (Vulkan/OpenGL)** - Hovering over "Current Present Mode" for Vulkan/OpenGL now shows the full swapchain tooltip (present mode, flags, back buffer, format, sync interval, etc.), same as DXGI. Tooltip content is API-aware (Vulkan/OpenGL get a short API line; DXGI flip state only for DXGI).
- **Swapchain tooltip** - DXGI flip state explanation (Composed/Independent/query failed) moved to a dedicated function for clarity; Vulkan/OpenGL tooltips no longer show DXGI flip-state text.

## v0.12.43 (2026-02-22)

- **Main tab (Vulkan/OpenGL)** - For non-DXGI APIs, the main tab now shows current present mode (e.g. FIFO, MAILBOX, IMMEDIATE for Vulkan) instead of only the DXGI "Prevent Tearing" option. Present mode line in VSync & Tearing section shows VkPresentModeKHR names for Vulkan.
- **Swapchain tooltip** - Present flags label and interpretation are now API-aware per ReShade: DXGI (DXGI_SWAP_CHAIN_FLAG), Vulkan (VkSwapchainCreateFlagsKHR), or generic for other APIs.

## v0.12.42 (2026-02-22)

- **Main tab** - "Prevent Tearing" checkbox is shown only for DXGI (DirectX 10/11/12); it has no effect on Vulkan/OpenGL.

## v0.12.41 (2026-02-22)

- **Hotkeys tab** - Brightness Up/Down hotkey step is now configurable (slider at bottom of Hotkeys tab, 1–50%). Default 5%.

## v0.12.40 (2026-02-22)

- **Main tab (Vulkan)** - When the graphics API is Vulkan, the DLSS subsection now shows NvLowLatencyVk.dll and vulkan-1.dll load and hook status (loaded / loaded (hooks active) / not loaded), matching the Vulkan tab.

## v0.12.39 (2026-02-22)

- **Hotkeys tab** - Each shortcut row has a "..." (Capture) button: click it, then press the key combination you want (numpad and modifiers supported). Existing shortcuts in config still load.
- **Hotkeys tab** - Added hotkey for moving to primary monitor (default: numpad+).
- **ADHD hotkey** - Fixed ADHD Multi-Monitor Mode hotkey so the Main tab checkbox stays in sync when toggled via shortcut.
- **Hotkeys config** - Shortcuts in hotkeys.toml are now stored as readable space-separated strings (e.g. `"ctrl a"`, `"alt numpad+"`). Defaults use this format. Plus-separated form (e.g. `ctrl+shift+m`) still loads for compatibility.

## v0.12.38 (2026-02-22)

- **Auto color space** - Madew "Auto Color Space" setting available only on dxgi, to prevent crashes.

## v0.12.37 (2026-02-22)

- **PCL stats (ETW)** - Disabled PCL Stats reporting for new, due to conflict with Native Frame Pacing code for Vulkan.

## v0.12.36 (2026-02-21)

- **SRWLOCK registry** - Added `g_wndproc_map_lock` (window proc HWND→WNDPROC map) so stuck-detection reports it; registry now has 16 global locks (18 total with logger + swapchain_tracking).

## v0.12.35 (2026-02-21)

- **ADHD Multi-Monitor** - The background overlay's message pump (PeekMessage/DispatchMessage) now runs on a dedicated thread instead of inside the continuous-monitoring Update() loop. If continuous monitoring stops, the overlay window still processes messages so the game is less likely to hang or crash from a full message queue.

## v0.12.34 (2026-02-21)

- **SRWLOCK registry** - All 15 global/static SRWLOCKs are now declared in `utils/srwlock_registry.hpp` and defined in `utils/srwlock_registry.cpp`. Stuck-detection log reports every lock (HELD/free) via `LogAllSrwlockStatus()`, making it easier to see which lock is hanging when diagnosing deadlocks.

## v0.12.33 (2026-02-21)

- **ADHD Multi-Monitor** - Background overlay is shown only when the game is in the foreground. Uses the same logic as `g_app_in_background`: extracted `IsAppInBackground()` (foreground PID vs current process PID via `GetForegroundWindow_Direct`) and use it for ADHD show/hide and in continuous monitoring.

## v0.12.31 (2026-02-21)

- **Load library hooks** - Release `g_module_srwlock` before calling `OnModuleLoaded` in all six load paths (LoadLibraryA/W, LoadLibraryExA/W, LoadPackagedLibrary, LdrLoadDll) to avoid deadlock or re-entrancy if the callback takes locks or triggers further loads.

## v0.12.25 (2026-02-21)

- **Changelog** - Deduplicate window restore/minimize entries (throttle, continue-rendering SIZE_RESTORED).
- **ApplyWindowChange** - Do not apply window position/size when the window is minimized; use `IsIconic_direct` (original API) so Continue Rendering’s spoof does not hide the real minimized state, avoiding incorrect SetWindowPos on minimized windows.

## v0.12.24 (2026-02-21)

- **Window minimize/restore (Win+Down / Win+Up)** - Throttle in ShowWindow_Direct: minimize and restore each limited to once per 100 ms per message type. When continue rendering is on, only WM_SIZE SIZE_MINIMIZED is suppressed; SIZE_RESTORED is delivered so the game can react when the user restores the window. Log when Win+Down minimizes the game window.

## v0.12.23 (2026-02-21)

- **Hotkeys tab** - Window and display hotkeys (Win+Down/Win+Up for minimize/restore, Win+Left/Win+Right for previous/next display) are now configurable in the Hotkeys tab; you can change or disable each shortcut (defaults: win+down, win+up, win+left, win+right). Hotkey parser supports the Win modifier.

## v0.12.22 (2026-02-21)

- **Win+Up grace period** - Win+Up (restore) works for a configurable time after the game lost focus (Advanced tab: 0–60 s or Forever).

## v0.12.21 (2026-02-21)

- **Improvements** - More reliable foreground/background detection for hotkeys, ADHD mode, and input handling.

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
- **Win+Down / Win+Up** - Minimize and restore borderless games (handled in ProcessHotkeys when app is in foreground; uses ShowWindow_Direct to prevent spoofing).

## v0.12.12 (2026-02-21)

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
