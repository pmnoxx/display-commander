# NVAPI Calls Inventory

## Overview

**Goal:** Single reference for all NVAPI functions invoked by the Display Commander addon (direct calls and hooks that forward to the driver). Use for driver compatibility checks, debugging, and refactors.

**Scope:** `src/addons/display_commander/` — all call sites of `NvAPI_*` and use of hooked NVAPI (e.g. `*_Original`, `*_Direct`).

---

## Call frequency (summary)

| Frequency | When / where |
|-----------|----------------|
| **Once per process/session** | `NvAPI_Initialize` (cached after first success), `NvAPI_Unload`, RunDLL path init, Reflex manager init, fullscreen-prevention enable/disable; DRS CreateSession/LoadSettings/DestroySession per logical “operation”. |
| **Once per second (or less)** | VRR/display ID: `TryQueryVrrStatusFromDxgiOutputDeviceName` (and thus `GetDisplayIdByDisplayName` + `GetVRRInfo`) — only when VRR/actual-refresh UI is enabled, within 5 s of foreground/background switch, throttled to 1 s (`continuous_monitoring.cpp`). `NvAPI_DISP_GetAdaptiveSyncData`: background thread at `refresh_rate_monitor_poll_ms` (default 1000 ms when graph off; configurable when graph on). |
| **Per frame (addon-driven)** | `NvAPI_D3D_SetSleepMode` (ApplySleepMode on present), `NvAPI_D3D_Sleep` (when not suppressed), `NvAPI_D3D_SetLatencyMarker` (multiple markers per frame: SIMULATION_START/END, RENDERSUBMIT_START/END, PRESENT_START/END) — all from swapchain/present path. |
| **Per UI frame (when tab visible)** | `NvAPI_D3D_GetSleepStatus` — once per ImGui frame while Advanced tab Reflex section is visible. |
| **Game-driven (hook invocations)** | When the game calls the hooked APIs: `SetSleepMode`, `Sleep`, `SetLatencyMarker`, `GetLatency`, `GetSleepStatus`, `GetHdrCapabilities`. Frequency = game-dependent (often per frame for Reflex games). |
| **On user action / rare** | All DRS APIs (profile search, set/delete DLSS preset, RunDLL SetDWORD, fullscreen prevention); `NvAPI_GetErrorMessage` (only on DRS error path); `NvAPI_SYS_GetDriverAndBranchVersion`, `NvAPI_EnumPhysicalGPUs` (fullscreen prevention diagnostics). |

---

## Summary by category

| Category | Functions | Purpose |
|----------|-----------|---------|
| **Init / teardown** | `NvAPI_Initialize`, `NvAPI_Unload`, `NvAPI_GetErrorMessage` | Session and error strings |
| **Display (DISP)** | `NvAPI_DISP_GetDisplayIdByDisplayName`, `NvAPI_Disp_GetVRRInfo`, `NvAPI_DISP_GetAdaptiveSyncData`, `NvAPI_Disp_GetHdrCapabilities` | Display ID, VRR, adaptive sync, HDR |
| **Reflex / latency (D3D)** | `NvAPI_D3D_SetSleepMode`, `NvAPI_D3D_Sleep`, `NvAPI_D3D_SetLatencyMarker`, `NvAPI_D3D_GetLatency`, `NvAPI_D3D_GetSleepStatus` | Reflex low latency (hooks + direct) |
| **Driver profile (DRS)** | `NvAPI_DRS_*` (CreateSession, LoadSettings, DestroySession, GetProfileInfo, FindApplicationByName, GetSetting, SetSetting, DeleteProfileSetting, SaveSettings, CreateProfile, CreateApplication, EnumSettings, EnumAvailableSettingValues) | Profile search, DLSS preset, RunDLL SetDWORD, fullscreen prevention |
| **System (SYS)** | `NvAPI_SYS_GetDriverAndBranchVersion`, `NvAPI_EnumPhysicalGPUs` | Driver version, GPU enumeration |

---

## 1. Init / teardown / error

| NVAPI function | Frequency | File(s) | Usage |
|----------------|-----------|---------|--------|
| `NvAPI_Initialize()` | Once per process/session (cached) | `continuous_monitoring.cpp`, `main_entry.cpp`, `reflex_manager.cpp`, `nvapi_fullscreen_prevention.cpp` | One-time or per-session init before other NVAPI use |
| `NvAPI_Unload()` | Once (on disable) | `nvapi_fullscreen_prevention.cpp` | Teardown when disabling fullscreen prevention |
| `NvAPI_GetErrorMessage(NvAPI_Status, NvAPI_ShortString)` | On DRS error only | `nvidia_profile_search.cpp` | `MakeNvapiError()` for user-facing DRS error messages |

---

## 2. Display (DISP)

| NVAPI function | Frequency | File(s) | Usage |
|----------------|-----------|---------|--------|
| `NvAPI_DISP_GetDisplayIdByDisplayName(const char*, NvU32*)` | ≤1/s (VRR/refresh UI, within 5 s of fg/bg switch) | `continuous_monitoring.cpp` | `ResolveDisplayIdByNameWithReinit()` — map DXGI output name (e.g. `\\.\DISPLAY1`) to NVAPI display ID for VRR/adaptive sync |
| `NvAPI_Disp_GetVRRInfo(NvU32 displayId, NV_GET_VRR_INFO*)` | ≤1/s (same conditions) | `continuous_monitoring.cpp` | VRR status (enabled/requested/possible, etc.); feeds Swapchain tab and cached `VrrStatus` |
| `NvAPI_DISP_GetAdaptiveSyncData(NvU32 displayId, NV_GET_ADAPTIVE_SYNC_DATA*)` | Poll: `refresh_rate_monitor_poll_ms` (default 1000 ms) | `nvapi_actual_refresh_rate_monitor.cpp` | Actual refresh rate (flip count/timestamp); used for Main tab graph and overlay |
| `NvAPI_Disp_GetHdrCapabilities(NvU32 displayId, NV_HDR_CAPABILITIES*)` | Game-driven (hook) | `hooks/nvapi_hooks.cpp` | HDR capabilities; hooked (detour) and may filter/spoof for HDR hiding feature |

---

## 3. Reflex / latency (D3D) — hooked and direct

All of these are used via **hooks** in `hooks/nvapi_hooks.cpp` (detours forward to `*_Original`). The addon also calls the **direct** wrappers (which call `*_Original`) from Reflex manager and UI.

| NVAPI function | Frequency | Hook/direct | File(s) | Usage |
|----------------|-----------|-------------|---------|--------|
| `NvAPI_D3D_SetSleepMode(IUnknown*, NV_SET_SLEEP_MODE_PARAMS*)` | Per frame (addon ApplySleepMode on present); game-driven when hooked | Hook + Direct | `nvapi_hooks.cpp`, `reflex_manager.cpp` | Set Reflex low-latency mode (UI + game) |
| `NvAPI_D3D_Sleep(IUnknown*)` | Per frame when addon calls (unless suppressed); game-driven when hooked | Hook + Direct | `nvapi_hooks.cpp`, `reflex_manager.cpp` | Reflex frame pacing (game or addon) |
| `NvAPI_D3D_SetLatencyMarker(IUnknown*, NV_LATENCY_MARKER_PARAMS*)` | Multiple per frame (6 markers: SIMULATION_START/END, RENDERSUBMIT_*, PRESENT_*); game-driven when hooked | Hook + Direct | `nvapi_hooks.cpp`, `reflex_manager.cpp`, `latency/pclstats.*` | Latency markers (SIMULATION_START etc.); PCLStats ETW |
| `NvAPI_D3D_GetLatency(IUnknown*, NV_LATENCY_RESULT_PARAMS*)` | Game-driven (hook) | Hook + Direct | `nvapi_hooks.cpp` | Query latency result (detour only; direct wrapper exists) |
| `NvAPI_D3D_GetSleepStatus(IUnknown*, NV_GET_SLEEP_STATUS_PARAMS*)` | Per UI frame when Advanced tab Reflex section visible; game-driven when hooked | Hook + Direct | `nvapi_hooks.cpp`, `reflex_manager.cpp` | Sleep status for UI (e.g. Reflex Low Latency on/off) |

---

## 4. Driver profile (DRS)

| NVAPI function | Frequency | File(s) | Usage |
|----------------|-----------|---------|--------|
| `NvAPI_DRS_CreateSession(NvDRSSessionHandle*)` | Per DRS operation (user action / RunDLL) | `nvidia_profile_search.cpp`, `nvapi_fullscreen_prevention.cpp` | Create DRS session |
| `NvAPI_DRS_LoadSettings(NvDRSSessionHandle)` | Per DRS operation | `nvidia_profile_search.cpp`, `nvapi_fullscreen_prevention.cpp` | Load driver profiles from disk |
| `NvAPI_DRS_DestroySession(NvDRSSessionHandle)` | Per DRS operation (after use) | `nvidia_profile_search.cpp`, `nvapi_fullscreen_prevention.cpp` | Destroy session |
| `NvAPI_DRS_GetProfileInfo(...)` | On profile search / RunDLL | `nvidia_profile_search.cpp` | Profile name/info for current exe |
| `NvAPI_DRS_FindApplicationByName(...)` | On profile search / RunDLL / fullscreen prevention | `nvidia_profile_search.cpp` | Find profile by app path (current exe) |
| `NvAPI_DRS_GetSetting(...)` | On profile read / set / enum | `nvidia_profile_search.cpp` | Read a profile setting (e.g. DLSS preset IDs) |
| `NvAPI_DRS_SetSetting(...)` | On set preset / fullscreen prevention | `nvidia_profile_search.cpp`, `nvapi_fullscreen_prevention.cpp` | Write a profile setting |
| `NvAPI_DRS_DeleteProfileSetting(...)` | On RunDLL clear or clear DLSS override | `nvidia_profile_search.cpp` | Reset setting to default (RunDLL “~” and ClearDriverDlssPresetOverride path) |
| `NvAPI_DRS_SaveSettings(NvDRSSessionHandle)` | After SetSetting/DeleteProfileSetting in same op | `nvidia_profile_search.cpp`, `nvapi_fullscreen_prevention.cpp` | Persist profile changes |
| `NvAPI_DRS_CreateProfile(...)` | Once when enabling fullscreen prevention (if new profile) | `nvapi_fullscreen_prevention.cpp` | Create new profile (fullscreen prevention) |
| `NvAPI_DRS_CreateApplication(...)` | Once when enabling fullscreen prevention (if new app) | `nvapi_fullscreen_prevention.cpp` | Add application to profile |
| `NvAPI_DRS_EnumSettings(...)` | On profile search (enumerate settings) | `nvidia_profile_search.cpp` | Enumerate settings in profile |
| `NvAPI_DRS_EnumAvailableSettingValues(...)` | On DLSS preset UI (allowed values) | `nvidia_profile_search.cpp` | Get allowed values for a setting (e.g. DLSS presets) |

---

## 5. System (SYS)

| NVAPI function | Frequency | File(s) | Usage |
|----------------|-----------|---------|--------|
| `NvAPI_SYS_GetDriverAndBranchVersion(NvU32* version, NvAPI_ShortString branch)` | Once when enabling fullscreen prevention (diagnostics) | `nvapi_fullscreen_prevention.cpp` | Driver version string for logging/UI |
| `NvAPI_EnumPhysicalGPUs(NvPhysicalGpuHandle*, NvU32*)` | Once when enabling fullscreen prevention (diagnostics) | `nvapi_fullscreen_prevention.cpp` | GPU count/handles (e.g. diagnostics) |

---

## File index (where each call lives)

| File | NVAPI calls |
|------|-------------|
| `continuous_monitoring.cpp` | `NvAPI_Initialize`, `NvAPI_DISP_GetDisplayIdByDisplayName`, `NvAPI_Disp_GetVRRInfo` |
| `main_entry.cpp` | `NvAPI_Initialize` (RunDLL_NvAPI_SetDWORD path) |
| `nvapi/reflex_manager.cpp` | `NvAPI_Initialize`, `NvAPI_D3D_SetSleepMode_Direct`, `NvAPI_D3D_SetLatencyMarker_Direct`, `NvAPI_D3D_Sleep_Direct`, `NvAPI_D3D_GetSleepStatus_Direct` |
| `nvapi/nvapi_actual_refresh_rate_monitor.cpp` | `NvAPI_DISP_GetAdaptiveSyncData` |
| `nvapi/nvidia_profile_search.cpp` | `NvAPI_GetErrorMessage`, `NvAPI_DRS_CreateSession`, `NvAPI_DRS_LoadSettings`, `NvAPI_DRS_DestroySession`, `NvAPI_DRS_GetProfileInfo`, `NvAPI_DRS_FindApplicationByName`, `NvAPI_DRS_GetSetting`, `NvAPI_DRS_SetSetting`, `NvAPI_DRS_DeleteProfileSetting`, `NvAPI_DRS_SaveSettings`, `NvAPI_DRS_EnumSettings`, `NvAPI_DRS_EnumAvailableSettingValues` |
| `nvapi/nvapi_fullscreen_prevention.cpp` | `NvAPI_Initialize`, `NvAPI_Unload`, `NvAPI_DRS_*` (CreateSession, LoadSettings, CreateProfile, CreateApplication, SetSetting, SaveSettings, DestroySession), `NvAPI_SYS_GetDriverAndBranchVersion`, `NvAPI_EnumPhysicalGPUs` |
| `hooks/nvapi_hooks.cpp` | All Reflex D3D calls as **Original** (driver); detours call `NvAPI_Disp_GetHdrCapabilities_Original` |

---

## Notes

- **Hooks:** `nvapi_hooks.cpp` installs detours for Reflex (SetSleepMode, Sleep, SetLatencyMarker, GetLatency, GetSleepStatus) and for `NvAPI_Disp_GetHdrCapabilities`. The addon calls the real driver via `*_Original` from the direct wrappers (e.g. `NvAPI_D3D_SetSleepMode_Direct` → `NvAPI_D3D_SetSleepMode_Original`).
- **VRR / display ID:** `vrr_status` uses data from `continuous_monitoring.cpp` (ResolveDisplayIdByNameWithReinit + NvAPI_Disp_GetVRRInfo). `nvapi_actual_refresh_rate_monitor` uses the cached display ID from VRR status and calls `NvAPI_DISP_GetAdaptiveSyncData` only.
- **DRS:** Used for (1) NVIDIA profile search / DLSS preset override (nvidia_profile_search), (2) RunDLL SetDWORD / Delete (main_entry → SetOrDeleteProfileSettingForExe), (3) fullscreen prevention (nvapi_fullscreen_prevention). Admin may be required for DRS writes.
- **Naming:** NVIDIA uses both `DISP` and `Disp` in names (e.g. `NvAPI_DISP_GetDisplayIdByDisplayName` vs `NvAPI_Disp_GetVRRInfo`). This document keeps the exact SDK names.

---

## References

- Project rule: `project_structure.mdc` — NVAPI is statically linked per NVIDIA guidance; no std locks (use SRWLOCK).
- Memories: Fake NVAPI (fake nvapi64 for non-NVIDIA), HDR hiding (DXGI + optional HDR capability spoofing), Reflex/PCLStats ETW.
