# RoGetActivationFactory_Detour: Special K vs Display Commander

Comparison of how **Special K** (SpecialKO/SpecialK on GitHub) and **Display Commander** implement the `RoGetActivationFactory` detour for Windows.Gaming.Input (WGI) suppression.

**Special K reference:** [SpecialKO/SpecialK `src/input/windows.gaming.input.cpp`](https://github.com/SpecialKO/SpecialK/blob/main/src/input/windows.gaming.input.cpp)  
**Our implementation:** `src/addons/display_commander/hooks/windows_gaming_input_hooks.cpp`

---

## 1. Which interfaces are targeted

| Aspect | Special K | Display Commander |
|--------|-----------|-------------------|
| **IIDs suppressed** | Same three: `IID_IGamepadStatics`, `IID_IGamepadStatics2`, `IID_IRawGameControllerStatics` | Same three (hardcoded IIDs `IID_IGamepadStatics_SK`, etc., same values as SK/SDK) |
| **Other WGI interfaces** | Pass-through (not intercepted for suppression) | Pass-through |
| **Other WinRT (non-WGI)** | Always pass-through | Pass-through when only the first block applies; see §4 |

Both sides only fail the same three factory requests; other WGI (e.g. racing wheel, arcade stick) and other WinRT remain intact when we don’t suppress.

---

## 2. Detour logic (when do we return E_NOTIMPL?)

### Special K

- **Condition to enter WGI handling:**  
  `config.input.gamepad.hook_windows_gaming` is true **and** `iid` is one of the three IIDs.
- **Per-IID behavior:**
  - **IRawGameControllerStatics:** If `config.input.gamepad.windows_gaming_input.blackout_api` → `return E_NOTIMPL`.
  - **IGamepadStatics2:** If `blackout_api` → `return E_NOTIMPL`. Else if `xinput.emulate`, calls Original and installs vtable hooks (e.g. `FromGameController`).
  - **IGamepadStatics:**  
    - Unity special case: if `UnityPlayer.dll` and `Rewired_WindowsGamingInput.dll` and not (background_render && …), they **clear** `blackout_api` so WGI is **not** disabled for that game.  
    - If `blackout_api` → `return E_NOTIMPL`.  
    - Else: call Original, then install many vtable hooks (add_GamepadAdded, remove_GamepadAdded, get_Gamepads, etc.) and optionally inject virtual gamepads (PlayStation).
- **Fallback:** For any other case (wrong IID, or not entering the three-IID block, or path that didn’t return earlier), the function ends with:  
  `return RoGetActivationFactory_Original(activatableClassId, iid, factory);`

So suppression is **purely** via the per-game “blackout” setting; no separate “Unity + suppress all three” branch that ignores blackout.

### Display Commander

- **First block (three IIDs only):**  
  If `iid` is one of the three and the relevant WGI suppression setting **and** `continue_rendering` → `return E_NOTIMPL`. (Relevant = `suppress_wgi_for_unity` when Unity, `suppress_wgi_for_non_unity_games` otherwise.)
- **Second block (Unity):**  
  If `UnityPlayer.dll` is loaded **and** `suppress_wgi_for_unity` → `return E_NOTIMPL` **for any `iid`** (any RoGetActivationFactory call).
- **Otherwise:**  
  `return RoGetActivationFactory_Original(activatableClassId, iid, factory);`

So we have two ways to suppress: (1) three IIDs + per-game-type WGI suppress + continue_rendering, (2) Unity loaded + suppress_wgi_for_unity (all IIDs). See §4 for the implication of (2).

---

## 3. Hook installation and scope

| Aspect | Special K | Display Commander |
|--------|-----------|-------------------|
| **Hook target** | `Combase.dll` → `RoGetActivationFactory` | Same |
| **Hook framework** | `SK_CreateDLLHook2` / `SK_EnableHook` | MinHook `CreateAndEnableHook` |
| **When hook is installed** | Deferred: dedicated thread waits for `Windows.Gaming.Input.dll` to be loaded (up to 30 s), then installs hook on Combase.dll | When “Suppress Windows.Gaming.Input” is on and not suppressed by HookSuppressionManager; no wait for WGI DLL |
| **Proactive trigger** | After hook installed, can call `RoGetActivationFactory_Original(L"Windows.Gaming.Input.Gamepad", IID_IGamepadStatics, …)` to trigger deferred WGI vtable hooks if the game hasn’t requested the factory yet | We do not proactively call the factory |
| **Condition to install** | WGI hooking enabled (`config.input.gamepad.hook_windows_gaming`) and hook installed from a one-time deferred init | Only if user has “Suppress Windows.Gaming.Input” enabled (and not suppressed by HookSuppressionManager) |

We do not use a deferred thread or proactive factory call; we hook as soon as we decide to install WGI hooks.

---

## 4. Differences and potential issues

### 4.1 Unity + suppress: all IIDs vs three IIDs

- **Us:** When `UnityPlayer.dll` is loaded and “Suppress Windows.Gaming.Input” is on, we return `E_NOTIMPL` for **every** `RoGetActivationFactory` call (second block does not check `iid`). That can break other WinRT use in the same process (e.g. other namespaces).
- **Special K:** They only return `E_NOTIMPL` when `blackout_api` is set, and only for the **three** WGI IIDs. For Unity they sometimes *disable* blackout (e.g. when Rewired_WindowsGamingInput is present and background_render conditions aren’t met).

**Recommendation:** Restrict the Unity branch to the same three IIDs, e.g. only return `E_NOTIMPL` when `(iUnityPlayer && suppress && is_blocked_iid)` so other WinRT factories are never suppressed.

### 4.2 Continue rendering requirement

- **Us:** We require **both** “Suppress Windows.Gaming.Input” **and** “Continue Rendering in Background” to suppress the three IIDs in the first block. If continue_rendering is off, we don’t suppress there (but the Unity block can still suppress all IIDs when Unity is loaded).
- **Special K:** Suppression is driven by `blackout_api` (per-game); no explicit “continue rendering” check in the RoGetActivationFactory detour.

So our behavior is intentionally tighter in the first block (suppress only when background rendering is desired); the discrepancy is mainly the Unity block applying to all IIDs.

### 4.3 Rich WGI handling in Special K

- Special K, when not blacking out, **hooks WGI vtables** (IGamepadStatics, IGamepadStatics2, IVectorView, etc.) to inject virtual PlayStation controllers and customize behavior. We do not; we only fail the three factory requests and pass the rest through to the original API.

---

## 5. Summary table

| Item | Special K | Display Commander |
|------|-----------|-------------------|
| Same three IIDs suppressed | ✓ | ✓ |
| Suppression = return E_NOTIMPL | ✓ (blackout_api) | ✓ (suppress + continue_rendering, or Unity+suppress) |
| Unity-specific handling | Can *disable* blackout for Rewired+WGI | Extra branch: Unity+suppress → E_NOTIMPL for **all** IIDs (possible bug) |
| Other WinRT factories | Never suppressed | Suppressed when Unity+suppress (see §4.1) |
| Hook on Combase.dll | ✓ | ✓ |
| Deferred init / wait for WGI DLL | ✓ (thread, up to 30 s) | ✗ |
| Vtable hooks on WGI factories | ✓ (many) | ✗ |
| Proactive factory call to trigger hooks | ✓ | ✗ |

---

## 6. References

- Special K: `src/input/windows.gaming.input.cpp` — `RoGetActivationFactory_Detour` (approx. lines 1350–1647), `SK_Input_HookWGI` (approx. 1650–1721).
- Our code: `windows_gaming_input_hooks.cpp` — `RoGetActivationFactory_Detour`, `InstallWindowsGamingInputHooks`, `UninstallWindowsGamingInputHooks`.
- IIDs: Same as Windows SDK / windows-rs; we use hardcoded GUIDs to avoid pulling in `windows.gaming.input.h`.
