**When releasing:** the version is stored in one place only. Update `src/addons/display_commander/CMakeLists.txt` (`DISPLAY_COMMANDER_VERSION_MAJOR`/`MINOR`/`PATCH`). CMake passes these into the build; `version.hpp` uses them and derives the version string. Do not edit `version.hpp` for version numbers. See `VERSION_BUMPING.md` for the bump script.

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
