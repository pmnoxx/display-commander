# Active Input APIs Display (Special K Comparison)

This document describes how **Special K** shows which input APIs are "active" (recently used by the game) and how **Display Commander** can implement a similar list on the **Controller tab**, e.g. "if the game read from XInput GetState within the last 10s, show XInput in the list of active APIs."

**Special K reference:** For local comparison, clone into `external-src/SpecialK` per [EXTERNAL_SRC.md](../EXTERNAL_SRC.md). Relevant input modules: `src/input/input.cpp`, `src/input/xinput_core.cpp`, `src/input/dinput8.cpp`, `src/input/raw_input.cpp`, `src/input/hid.cpp`, `src/input/windows.gaming.input.cpp`, `src/input/winmm_joystick.cpp`.

---

## 1. How Special K (and we) can determine "active" input APIs

### 1.1 Timestamp-based approach

- **Idea:** For each input-related API the game can call (XInput, DirectInput, Raw Input, HID, Windows.Gaming.Input, winmm joystick), we **hook** that API and on each call record the **current time** (e.g. `utils::get_now_ns()`).
- **Active:** An input API is considered **active** if it has been called at least once within the last **N seconds** (e.g. 10s). So we store a **last-call timestamp** per API (or per hook) and in the UI show only those where `(now_ns - last_call_time_ns) < N * 1e9`.

This matches the typical pattern for "game is using this API right now" without requiring Special K source: any hook-based input layer can do the same.

### 1.2 What Special K hooks (input APIs to consider)

From Special K’s input layout, the **input APIs** that games use and that we may want to show as "active" are:

| Input API / layer        | Special K module              | Display Commander hooks / groups |
|--------------------------|-------------------------------|----------------------------------|
| **XInput**                | xinput_core.cpp               | XINPUT1_4: XInputGetState, XInputGetStateEx, XInputSetState, XInputGetCapabilities |
| **DirectInput 8**        | dinput8.cpp                   | DINPUT8: DirectInput8Create (+ device GetDeviceState etc. in some codebases) |
| **DirectInput (legacy)** | dinput7.cpp                   | DINPUT: DirectInputCreate |
| **Raw Input**            | raw_input.cpp                 | USER32: GetRawInputData, GetRawInputBuffer |
| **HID**                  | hid.cpp                       | HID_API: CreateFileW/A (HID paths), ReadFile, HidD_GetInputReport, HidD_GetAttributes, etc. |
| **Windows.Gaming.Input** | windows.gaming.input.cpp      | RoGetActivationFactory for WGI (not in central hook stats; can track separately if desired) |
| **winmm joystick**       | winmm_joystick.cpp            | WINMM_JOYSTICK: joyGetPos, joyGetPosEx |

We already have **hook call stats** (total_calls, unsuppressed_calls) for most of these in `windows_message_hooks` (and HID in `hid_suppression_hooks`, XInput in `xinput_hooks`, DInput in `dinput_hooks`, winmm in `winmm_joystick_hooks`). To show "active in last 10s" we need a **last-call timestamp** per hook (or per logical "input API" group).

### 1.3 Grouping hooks into "input APIs" for the UI

For the Controller tab we want a short list of **API names**, not every single hook. Suggested mapping:

- **XInput** — active if any of: XInputGetState, XInputGetStateEx, XInputSetState, XInputGetCapabilities had a call in the last 10s.
- **DirectInput 8** — active if DirectInput8Create (or DInput8 device state reads, if we hook them) in last 10s.
- **DirectInput** — active if DirectInputCreate in last 10s.
- **Raw Input** — active if GetRawInputData or GetRawInputBuffer in last 10s.
- **HID** — active if any HID_API hook (CreateFile to HID path, ReadFile, HidD_GetInputReport, etc.) in last 10s.
- **Windows.Gaming.Input** — active if RoGetActivationFactory was used for WGI in last 10s (optional; may need separate timestamp).
- **winmm joystick** — active if joyGetPos or joyGetPosEx in last 10s.

So we need **last_call_time_ns** (or equivalent) for each hook that belongs to one of these groups, then the UI aggregates by group and shows "Active input APIs (last 10s): XInput, Raw Input, HID", etc.

---

## 2. Display Commander: current state and required changes

### 2.1 Current hook stats

- **HookCallStats** (in `windows_message_hooks.hpp`): `total_calls`, `unsuppressed_calls` only. No timestamp.
- **XInput:** We already have `last_xinput_call_time_ns` in `XInputSharedState` (xinput_widget), updated in `ProcessXInputGetState`. So "XInput active in last 10s" can be implemented using that without changing the central hook stats.
- **Other input hooks:** They only increment `g_hook_stats[i].increment_total()` (and optionally `increment_unsuppressed()`). There is no `last_call_time_ns` in the central `HookCallStats` array.

### 2.2 Options

- **A) Add `last_call_time_ns` to HookCallStats**  
  Every hook that we care about for "active input API" would update this field (e.g. `last_call_time_ns.store(utils::get_now_ns())`). UI iterates over the input-related hook indices and checks `(now_ns - last_call_time_ns) < 10*1e9`.  
  **Pros:** Single place for all hooks; consistent with existing hook stats.  
  **Cons:** Slightly more memory and one extra store in hot paths (input detours).

- **B) Keep separate timestamps only for "input API" groups**  
  E.g. one atomic last-call time per group (XInput, DInput8, DInput, Raw Input, HID, WGI, winmm). Each detour in that group updates the group’s timestamp.  
  **Pros:** Fewer atomics, simpler UI (one check per API name).  
  **Cons:** Need to touch multiple detour files to update the right group; less granular (can’t show "GetRawInputData vs GetRawInputBuffer" separately without more structure).

Recommendation: **Option A** — add `std::atomic<uint64_t> last_call_time_ns{0}` to `HookCallStats`, update it in all input-related detours (XInput, DInput, DInput8, user32 Raw Input, HID, winmm joystick; and WGI if we add a timestamp there). For XInput we can either keep using `last_xinput_call_time_ns` for the "XInput" group or also set the central stats’ `last_call_time_ns` for the four XInput hooks so the Controller tab can use a single source of truth.

### 2.3 Which hooks must update last_call_time_ns

At minimum, in each detour that is considered "input API" for the Controller tab:

- **XINPUT1_4:** HOOK_XInputGetState, HOOK_XInputGetStateEx, HOOK_XInputSetState, HOOK_XInputGetCapabilities (all in xinput_hooks.cpp; already have shared_state last time, can also set g_hook_stats[i].last_call_time_ns).
- **USER32 (Raw Input):** HOOK_GetRawInputData, HOOK_GetRawInputBuffer (windows_message_hooks.cpp).
- **DINPUT8:** HOOK_DInput8CreateDevice (dinput_hooks.cpp).
- **DINPUT:** HOOK_DInputCreateDevice (dinput_hooks.cpp).
- **HID_API:** HOOK_HID_CreateFileA, HOOK_HID_CreateFileW, HOOK_HID_ReadFile, HOOK_HID_ReadFileEx, HOOK_HID_ReadFileScatter, HOOK_HIDD_GetInputReport, HOOK_HIDD_GetAttributes (and optionally other HidD_* if we want "HID" to include enumeration; hid_suppression_hooks.cpp).
- **WINMM_JOYSTICK:** HOOK_joyGetPos, HOOK_joyGetPosEx (winmm_joystick_hooks.cpp).
- **Windows.Gaming.Input:** If we want it in the list, the RoGetActivationFactory detour in windows_gaming_input_hooks.cpp would need to record a timestamp (could be a single global "last WGI factory request" or we add a synthetic hook index / group for WGI).

Reset: When the user clicks "Reset" hook stats, also set `last_call_time_ns = 0` for all hooks (or at least for input-related ones) so "Active input APIs" clears.

---

## 3. UI: Controller tab "Active input APIs (last 10s)"

- **Placement:** On the Controller tab, e.g. at the top or after a short intro, a **CollapsingHeader** "Active input APIs (last 10s)".
- **Content:** A list of **API names** (XInput, DirectInput 8, DirectInput, Raw Input, HID, Windows.Gaming.Input, winmm joystick) for which at least one associated hook has `(now_ns - last_call_time_ns) < 10*1e9`. If none are active, show e.g. "None (no input API calls in the last 10 seconds)."
- **Tooltip:** "APIs the game has used recently (at least one call in the last 10 seconds). Similar to Special K’s input API display."
- **Update:** Each frame the tab is drawn, compute `now_ns = utils::get_now_ns()` and compare; no background thread needed.

This gives the user the same kind of information as Special K: which input stacks the game is actually using right now.

---

## 4. Summary

| Aspect | Special K (inferred) | Display Commander (plan) |
|--------|----------------------|---------------------------|
| **Detection** | Hooks on XInput, DInput, Raw Input, HID, WGI, winmm, etc. | Same; we already have these hooks. |
| **"Active" rule** | Presumably "called recently" (time or frame window). | Call in last 10 seconds (`last_call_time_ns`). |
| **Storage** | Unknown (likely per-API or per-hook timestamp). | Add `last_call_time_ns` to `HookCallStats`; update in input detours. |
| **UI** | Lists active input APIs (exact location in SK UI not verified in this doc). | Controller tab: "Active input APIs (last 10s)" with API names. |

Implementing this requires: (1) extending `HookCallStats` with `last_call_time_ns`, (2) updating that field in all input-related detours, (3) adding a helper that maps hook indices to "input API" names and checks the 10s window, (4) drawing the list on the Controller tab.

---

## 5. Plan: Additional hooks for more complete active gamepad API detection

The following extensions would improve coverage of "active input APIs" so the list reflects more ways a game can read gamepad/controller input.

### 5.1 Windows.Gaming.Input (WGI)

- **Current:** RoGetActivationFactory is hooked in `windows_gaming_input_hooks.cpp`; `g_wgi_state.wgi_called` is set when a WGI factory IID is requested, but there is no timestamp and no entry in the central hook stats.
- **Plan:** In `RoGetActivationFactory_Detour`, when `is_blocked_iid` is true (or when the class id is a WGI gamepad class), store `last_call_time_ns = utils::get_now_ns()` in a global atomic (e.g. `g_wgi_state.last_wgi_factory_call_ns`). In the Controller tab "Active input APIs" logic, treat **Windows.Gaming.Input** as active if `(now_ns - last_wgi_factory_call_ns) < 10*1e9`. No new hook; just add the timestamp and wire it into the existing active-API list.

### 5.2 DirectInput device polling (GetDeviceState / GetDeviceData)

- **Current:** We hook `DirectInput8Create` and `DirectInputCreate` (creation only). We also install **per-device** vtable hooks on `IDirectInputDevice8` for `GetDeviceState` and `GetDeviceData` in `dinput_hooks.cpp` (used for input blocking). Those detours do **not** update `g_hook_stats` or any shared "last call" timestamp, so "DirectInput 8" / "DirectInput" only appears active when the game creates a device, not when it polls.
- **Plan:** In `DInputDevice_GetDeviceState_Detour` and `DInputDevice_GetDeviceData_Detour`, update a **shared** last-call timestamp for the DirectInput 8 group (e.g. a global `std::atomic<uint64_t> g_last_dinput8_poll_ns`). Optionally a separate one for legacy DInput if we can distinguish device type. Expose these (or a single "DInput poll" timestamp) and in the active-API UI treat "DirectInput 8" (and "DirectInput") as active if either Create was called in the last 10s **or** the poll timestamp is within the last 10s. Alternative: add synthetic hook indices for "DInput8 GetDeviceState" / "DInput8 GetDeviceData" and update them from the detours (requires a way to update last_call_time_ns for an index that is not in the per-DLL hook table, or a separate small table for "virtual" input APIs).

### 5.3 HID: hooks that exist but don’t update last_call_time_ns

- **Current:** `hid_additional_hooks.cpp` implements detours for `HidD_GetPreparsedData`, `HidD_FreePreparsedData`, `HidP_GetCaps` and increments `g_hook_stats[HOOK_HIDD_GetPreparsedData]` etc., but does **not** call `UpdateHookLastCallTime`. So when a game only uses these (e.g. to enumerate or classify HID devices), "HID" may not show as active.
- **Plan:** In each detour in `hid_additional_hooks.cpp` that increments `g_hook_stats`, add `UpdateHookLastCallTime(static_cast<int>(HOOK_xxx))`. Ensure the Controller tab "HID" group includes these hook indices: `HOOK_HIDD_GetPreparsedData`, `HOOK_HIDD_FreePreparsedData`, `HOOK_HIDD_GetCaps` (and any other HID hooks in that file that have a `HookIndex`). Optionally add `UpdateHookLastCallTime` for `HOOK_HID_WriteFile`, `HOOK_HID_DeviceIoControl` if those detours exist and are used for gamepad-related I/O (rumble, feature reports).

### 5.4 HID: hooks we might add (more HidD_* / HidP_*)

- **Current:** We have central hook indices for `HIDD_GetFeature`, `HIDD_SetFeature`, `HIDD_GetProductString`, etc. If detours are not installed for these (or are in a different code path), games that only use e.g. `HidD_GetFeature` for DualSense won’t be detected.
- **Plan:** If any HidD_* / HidP_* detours are added (e.g. for feature reports or device strings), register them in the same HID_API group and call `UpdateHookLastCallTime` so "HID" stays accurate.

### 5.5 XInput: XInputGetKeystroke

- **Current:** We hook XInputGetState, XInputGetStateEx, XInputSetState, XInputGetCapabilities. We do **not** hook `XInputGetKeystroke` (used for guide button and key events on some titles).
- **Plan:** Add a hook for `XInputGetKeystroke` from xinput1_4 (and other xinput*.dll variants if desired). Add a new `HookIndex` (e.g. `HOOK_XInputGetKeystroke`) and include it in the "XInput" group for the active-API list. Gives more accurate "XInput" activity when the game only polls keystrokes.

### 5.6 Raw Input: RegisterRawInputDevices / GetRawInputDeviceInfo

- **Current:** We hook `GetRawInputData` and `GetRawInputBuffer`. Games that use Raw Input typically also call `RegisterRawInputDevices` (and sometimes `GetRawInputDeviceList` / `GetRawInputDeviceInfo`) to register for HID/mouse/keyboard.
- **Plan:** If we add hooks for `RegisterRawInputDevices` or `GetRawInputDeviceInfo`, update their `last_call_time_ns` and include them in the "Raw Input" group. That way "Raw Input" can show as active when the game only registers for devices or queries device info, even before the first GetRawInputData/GetRawInputBuffer call in a session.

### 5.7 GameInput (Windows SDK)

- **DLL:** The **IGameInput** API is provided by **GameInput.dll** (Microsoft Game Input redist; e.g. `winget install Microsoft.GameInput`). The main entry point is **`GameInputCreate`** (returns `IGameInput**`). We do not link the SDK; we hook via `GetProcAddress(hModule, "GameInputCreate")` when the game loads GameInput.dll.
- **Implemented:** We hook **GameInputCreate** in `game_input_hooks.cpp` when LoadLibrary loads **GameInput.dll**. On each call we mark **GameInput** as active in `InputActivityStats`, so the Controller tab shows "GameInput" in "Active input APIs (last 10s)". WGI (Windows.Gaming.Input) remains separate and is hooked via RoGetActivationFactory from combase when **Windows.Gaming.Input** DLLs are loaded.

### 5.8 Steam Input

- **Current:** We do not hook Steam Input APIs. Special K has `src/input/steam.cpp` for Steam controller handling.
- **Plan (optional):** If we ever hook Steam input (e.g. to show or suppress it), add "Steam Input" to the active-API list with a last-call timestamp. Low priority unless we add Steam-specific features.

### 5.9 Summary table (additional hooks for active-API display)

| Item | Type | Where | Action |
|------|------|--------|--------|
| WGI | Existing detour | windows_gaming_input_hooks.cpp | Add `last_wgi_factory_call_ns`; show "Windows.Gaming.Input" when within 10s. |
| DInput GetDeviceState/GetDeviceData | Existing vtable detours | dinput_hooks.cpp | Add shared `last_dinput8_poll_ns` (and optionally DInput); update in detours; use in active-API check. |
| HID (additional_hooks) | Existing detours | hid_additional_hooks.cpp | Call `UpdateHookLastCallTime` for HOOK_HIDD_GetPreparsedData, HOOK_HIDD_FreePreparsedData, HOOK_HIDD_GetCaps; add these to "HID" group in UI. |
| HID WriteFile/DeviceIoControl | Maybe existing | hid_suppression or elsewhere | If detours exist and update stats, add `UpdateHookLastCallTime`; include in "HID" group. |
| XInputGetKeystroke | New hook | xinput_hooks.cpp | Add hook + HookIndex; add to "XInput" group. |
| RegisterRawInputDevices / GetRawInputDeviceInfo | New hooks (optional) | user32 / windows_message_hooks | Add hooks + last_call_time_ns; add to "Raw Input" group. |
| GameInput | Implemented | game_input_hooks.cpp | Hook GameInputCreate from GameInput.dll; "GameInput" in InputActivityStats. |
| Steam Input | New hooks (optional) | New file | Add "Steam Input" if we hook Steam input. |
