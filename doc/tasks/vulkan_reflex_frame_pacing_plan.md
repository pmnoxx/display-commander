# Vulkan Reflex & Native Frame Pacing – Task Plan

**Goal:** Support native frame pacing for Vulkan games by hooking Vulkan Reflex APIs (similar to D3D `NvAPI_D3D_SetLatencyMarker_Detour`) and injecting the existing FPS limiter at the appropriate markers.

**Reference:** [Special K reflex.cpp (Vulkan section)](https://github.com/SpecialKO/SpecialK/blob/a69d3a0d3a751decb065715bf3a95317de60c7df/src/render/reflex/reflex.cpp#L1233-L1704) – NvLowLatencyVk.dll hooks and VK_NV_low_latency2 handling.

---

## 1. Overview

- **D3D today:** We hook `nvapi64.dll` (via `nvapi_QueryInterface`) for `NvAPI_D3D_SetLatencyMarker`, `NvAPI_D3D_SetSleepMode`, `NvAPI_D3D_Sleep`. In `NvAPI_D3D_SetLatencyMarker_Detour` we call `ChooseFpsLimiter`, and at `SIMULATION_START` / `PRESENT_START` / `PRESENT_END` we drive `OnPresentFlags2` / `HandlePresentAfter` for frame pacing.
- **Vulkan target:** Same idea on Vulkan – intercept Reflex latency markers and Sleep so we can run our FPS limiter at the right points and achieve native-style frame pacing for Vulkan.

Vulkan Reflex is exposed in two ways; we need to support both for broad game coverage:

| Path | DLL / entry | APIs | Notes |
|------|--------------|------|--------|
| **NvLowLatencyVk** | `NvLowLatencyVk.dll` | `NvLL_VK_InitLowLatencyDevice`, `NvLL_VK_SetSleepMode`, `NvLL_VK_Sleep`, `NvLL_VK_SetLatencyMarker` | Older path; hook by loading DLL and `GetProcAddress` + MinHook (like we do for nvapi). |
| **VK_NV_low_latency2** | `vulkan-1.dll` (loader) | `vkSetLatencyMarkerNV`, `vkGetLatencyTimingsNV` (via `vkGetDeviceProcAddr`) | Newer extension; hook loader’s `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` and wrap the returned function pointers. |

---

## 2. Vulkan (experimental) tab

- Add a **“Vulkan (experimental)”** tab, gated by a main-tab setting (e.g. `show_vulkan_tab`), similar to “Show Debug Tab” / experimental tab.
- Tab visibility: only when the setting is on (and optionally when Vulkan backend is detected or when hooks are available).
- Tab content (to be refined in later iterations):
  - Enable/disable Vulkan Reflex hooks (NvLowLatencyVk and/or vulkan-1 loader).
  - Status: which path is active (NvLL vs VK_NV_low_latency2), whether frame pacing is applied.
  - Optional: FPS limit / pacing options specific to Vulkan (reuse same limiter backend as D3D where possible).
- Implementation: new tab in `ui/new_ui/` (e.g. `vulkan_tab.cpp/.hpp`), add to `new_ui_tabs.cpp` and a `show_vulkan_tab` (or similar) in main tab settings; handle `tab_id == "vulkan"` in the visibility logic.

---

## 3. Hooking strategies

### 3.1 NvLowLatencyVk.dll (recommended first iteration)

- **When:** Load/hook when the process loads `NvLowLatencyVk.dll` (e.g. via LoadLibrary hook or early hook on `vulkan-1.dll` so we run before the game gets the proc addrs).
- **How:** Same pattern as NVAPI – `GetProcAddress(hNvLowLatencyVk, "NvLL_VK_...")` then `MH_CreateHook` for:
  - `NvLL_VK_SetLatencyMarker` – detour and at SIMULATION_START / INPUT_SAMPLE (and optionally PRESENT_*) call into the same FPS limiter / present logic we use for D3D Reflex.
  - `NvLL_VK_Sleep` – detour to optionally run our limiter wait before/after calling original.
  - `NvLL_VK_SetSleepMode` – detour to read/override `minimumIntervalUs` (and low latency flags) for FPS limit, similar to D3D SetSleepMode.
- **Structs:** Define minimal NvLL_VK_* parameter structs and marker types (or include a small header) so we don’t depend on full Vulkan SDK in the addon if not needed. Reference: Special K’s `NVLL_VK_LATENCY_MARKER_PARAMS`, `NVLL_VK_SET_SLEEP_MODE_PARAMS`, etc.

### 3.2 vulkan-1.dll (Vulkan loader) – VK_NV_low_latency2

- **Entry points to hook:** `vkGetInstanceProcAddr` and/or `vkGetDeviceProcAddr` from `vulkan-1.dll` (loaded by the game).
- **When:** Hook loader early (e.g. when we hook LoadLibrary or when we detect Vulkan in use). When the game calls `vkGetDeviceProcAddr(device, "vkSetLatencyMarkerNV")`, return our **wrapper** instead of the real pointer; store the real pointer and call it after our logic.
- **Wrapper behavior:** On `vkSetLatencyMarkerNV` (or equivalent `VkSetLatencyMarkerInfoNV`-style call), map marker type to D3D-like semantics (SIMULATION_START, INPUT_SAMPLE, PRESENT_*, etc.) and call the same FPS limiter / present hooks we use in `NvAPI_D3D_SetLatencyMarker_Detour` (e.g. `ChooseFpsLimiter`, `OnPresentFlags2`, `HandlePresentAfter` where appropriate).
- **Optional:** Also wrap `vkGetLatencyTimingsNV` if we need to expose latency stats in the Vulkan tab or elsewhere.

---

## 4. Integration with existing frame pacing

- Reuse **one** FPS limiter and present pipeline for both D3D and Vulkan:
  - `ChooseFpsLimiter` / `GetChosenFpsLimiter` (FpsLimiterCallSite could gain e.g. `reflex_marker_vk`).
  - `OnPresentFlags2`, `HandlePresentAfter`, `RecordNativeFrameTime` – call from Vulkan detours at the same logical points (SIMULATION_START vs PRESENT_START vs PRESENT_END as configured by existing options like `native_pacing_sim_start_only`).
- Avoid duplicating limiter logic; keep Vulkan as another “call site” that drives the same globals and present path.

---

## 5. Suggested iterations (phased implementation)

Implement in small steps; each step should be reviewable and optionally toggleable.

1. **Phase 1 – Plan and structure (this doc)**
   - [x] Write this plan in `doc/tasks`.
   - [ ] Optional: Add “Vulkan (experimental)” tab shell only (no hooks yet), with a setting to show the tab.

2. **Phase 2 – NvLowLatencyVk.dll hooks**
   - [x] Load `NvLowLatencyVk.dll` (or hook its load) and resolve `NvLL_VK_SetLatencyMarker`, `NvLL_VK_Sleep`, `NvLL_VK_SetSleepMode`.
   - [x] Implement detours; in the SetLatencyMarker detour, at the chosen marker type(s), call existing FPS limiter / present logic (no new limiter).
   - [x] Gate by a Vulkan-tab or global “enable Vulkan Reflex hooks” setting.
   - [ ] Verify in one Vulkan + Reflex game (e.g. title known to use NvLL).

3. **Phase 3 – vulkan-1.dll loader hooks (VK_NV_low_latency2)**
   - [x] Hook `vkGetDeviceProcAddr` in `vulkan-1.dll` (when vulkan-1.dll is loaded via LoadLibrary).
   - [x] Intercept requests for `vkSetLatencyMarkerNV`; return wrapped pointer; store real pointer and call it from wrapper.
   - [x] In the wrapper, map VK marker types to existing Reflex semantics and call the same limiter/present path as D3D (reflex_marker_vk, OnPresentFlags2, RecordNativeFrameTime, HandlePresentAfter).
   - [x] Vulkan tab: enable/disable vulkan-1 hooks, show "VK_NV_low_latency2 hooks active" and debug state (marker count, last marker, presentID).

4. **Phase 4 – Vulkan tab and settings**
   - [x] Vulkan (experimental) tab: enable/disable NvLL hooks, enable/disable vulkan-1 hooks, status (which path active), link to FPS limit (reuse main/advanced settings).
   - [x] Persist “Vulkan Reflex hooks” (vulkan_nvll_hooks_enabled, vulkan_vk_loader_hooks_enabled) in main tab settings.

5. **Phase 5 – Tuning and edge cases**
   - [ ] Handle multiple devices/swapchains if needed (track per-device or per-swapchain like D3D).
   - [ ] Optional: NvLL_VK_InitLowLatencyDevice detour to track “Reflex initialized” state.
   - [ ] Testing on several Vulkan Reflex games (NvLL path and VK_NV_low_latency2 path).

---

## 6. Technical notes

- **NvLL vs VK_NV_low_latency2:** Many Vulkan Reflex games (e.g. Doom) use only the extension path: they get `vkSetLatencyMarkerNV` via `vkGetDeviceProcAddr` from vulkan-1.dll and never load NvLowLatencyVk.dll. So `NvLL_VK_SetLatencyMarker` is not called for those titles; the vulkan-1 loader hook (Phase 3) wraps `vkSetLatencyMarkerNV` instead. Special K uses the same split: `SK_CreateDLLHook2` on NvLowLatencyVk.dll for the NvLL path, and the loader’s `vkSetLatencyMarkerNV` for VK_NV_low_latency2.
- **Already-loaded loader:** If vulkan-1.dll (or NvLowLatencyVk.dll) is loaded before our LoadLibrary hook runs, we install the corresponding hooks right after InstallLoadLibraryHooks() when the setting is enabled, so we don’t miss them.
- **No std::mutex:** Use SRWLOCK / `utils::SRWLock*` for any new shared state (per project rules).
- **MinHook:** Same pattern as `nvapi_hooks.cpp`: resolve target via GetProcAddress (or QueryInterface equivalent), then `CreateAndEnableHook`.
- **Vulkan types:** For VK_NV_low_latency2 we need minimal Vulkan types (e.g. `VkDevice`, `VkSwapchainKHR`, marker enum). Either a small local header or conditional include of Vulkan headers; avoid pulling in full SDK if not necessary.
- **ReShade:** ReShade may not inject into Vulkan runtimes the same way as D3D; confirm that the addon is loaded in Vulkan games and that hooking vulkan-1.dll and NvLowLatencyVk.dll is feasible (e.g. LoadLibrary order, 32/64-bit).

---

## 7. Success criteria

- Vulkan games using **NvLowLatencyVk** or **VK_NV_low_latency2** can use the same FPS limiter and frame pacing behavior as D3D Reflex games.
- One “Vulkan (experimental)” tab to enable/disable and observe Vulkan Reflex hooks.
- No duplicate limiter logic; single code path for “Reflex-style” pacing, with D3D and Vulkan as two call sites.

---

## 8. References

- Special K reflex.cpp (Vulkan): https://github.com/SpecialKO/SpecialK/blob/a69d3a0d3a751decb065715bf3a95317de60c7df/src/render/reflex/reflex.cpp#L1233-L1704
- NVAPI D3D Reflex hooks in this repo: `src/addons/display_commander/hooks/nvapi_hooks.cpp` (`NvAPI_D3D_SetLatencyMarker_Detour`, FPS limiter at PRESENT_START / SIMULATION_START / PRESENT_END).
- Tab registration: `src/addons/display_commander/ui/new_ui/new_ui_tabs.cpp` (`AddTab`, `tab_id == "experimental"` for visibility).
