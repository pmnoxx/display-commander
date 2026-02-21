# Special K vs Display Commander: "Continue Rendering in Background"

This document compares **Special K**'s "Render While Window is in Background" (`Window.System` / `RenderInBackground`) with **Display Commander**'s "Continue Rendering in Background" and lists what we have and what we may be missing.

**Special K reference:** `external-src/SpecialK` (config: `config.window.background_render`, `game_window.wantBackgroundRender()`).

---

## 1. API hooks (focus/foreground spoofing)

| Feature | Special K | Display Commander |
|--------|-----------|-------------------|
| **GetFocus** | When `wantBackgroundRender()`: if caller is **game thread** → return game HWND; if caller is **other process** → return 0. Other threads in same process get real `GetFocus`. | When `continue_rendering`: if foreground isn’t ours → return game window. **No thread/process check** – any thread in our process gets game window. |
| **GetForegroundWindow** | When `wantBackgroundRender()` and **not** true fullscreen and **not** SDL → return game HWND. (SDL: no spoof to avoid breaking focus handling.) | When `continue_rendering` and foreground isn’t ours → return game window. No fullscreen/SDL checks. |
| **GetActiveWindow** | **Spoofing disabled** (`#if 0` in `GetActiveWindow_Detour`). | **We spoof**: return game window when `continue_rendering` (with thread checks). |
| **GetGUIThreadInfo** | When `wantBackgroundRender()` and `idThread == game thread` → set `hwndActive`, `hwndFocus` to game window. | When `continue_rendering` and `idThread == game thread` **or** `idThread == 0` → set `hwndActive`, `hwndFocus` (and `hwndCaret`). |
| **SetForegroundWindow** | Not restricted (original called). | Not restricted (original called). |

**Possible gap:** Special K only spoofs **GetFocus** when the **caller is the game thread**; other threads (e.g. overlay) get real focus. We spoof for any thread in our process. For ReShade this may be acceptable; for strict compatibility we could add a game-thread check in `GetFocus_Detour` (and optionally `GetForegroundWindow_Detour`).

---

## 2. Message handling (window proc / message hooks)

Both suppress or alter activation/deactivation so the game keeps thinking it’s active.

| Message | Special K | Display Commander |
|---------|-----------|-------------------|
| **WM_ACTIVATE** (WA_INACTIVE) | When background_render: call `DefWindowProc`, inject `WM_KILLFOCUS`, optionally `ActivateWindow(hwnd, false)`, **return 0** (block). | When continue_rendering: **return true** (suppress). No DefWindowProc. |
| **WM_KILLFOCUS** | Injected when blocking deactivation; also block real WM_KILLFOCUS. | Suppress and send fake activation messages. |
| **WM_ACTIVATEAPP** (wParam FALSE) | Call `DefWindowProc`, inject `WM_KILLFOCUS`, **return 0**. “Blocking this message helps with many games that mute audio in background.” | Suppress and send fake activation. |
| **WM_NCACTIVATE** (deactivate) | Call `DefWindowProc`, inject `WM_KILLFOCUS`, `ActivateWindow(hwnd, false)`, **return 0/1**. | Suppress (return true). |
| **WM_MOUSEACTIVATE** | When background_render: `ActivateWindow(hwnd, true)`, return **MA_ACTIVATE** or **MA_ACTIVATEANDEAT** (so message is “eaten” or passed). | Suppress entire message (no MA_* return). |
| **WM_SHOWWINDOW** (hide) | When background_render: allow hide but “prevent the game from seeing it” (DefWindowProc then block?). | Suppress when wParam == FALSE. |

**Possible gap:** Special K often **calls DefWindowProc** for the deactivation message and then **injects WM_KILLFOCUS** and blocks the message. We only suppress. So the window might not get the same internal state updates as under SK (e.g. for audio “mute in background”).
**Possible gap:** For **WM_MOUSEACTIVATE** we don’t return **MA_ACTIVATE** / **MA_ACTIVATEANDEAT**; we remove the message. SK keeps the message semantics (activate and optionally eat). Depends whether our message hook can return MA_* or only suppress.

---

## 3. Background FPS and power

| Feature | Special K | Display Commander |
|--------|-----------|-------------------|
| **Background FPS limit** | `Render.FrameRate` / **BackgroundFPS** (0 = same as foreground). When window in background, apply this limit; when 0, no limit in background. | **Background FPS** setting; `GetTargetFps()` uses `g_app_in_background` and applies background limit. |
| **No Render in Background** | Concept exists (games that throttle when unfocused). | **no_render_in_background** + power-saving (skip dispatch/draw when background). |
| **No Present in Background** | Similar idea. | **no_present_in_background** (main tab). |
| **Power throttling** | `SK_Framerate_SetPowerThrottlingPolicy`: when **not** background or when BackgroundFPS &gt; 0. | We have power-saving toggles; no explicit “background FPS policy” like SK. |
| **VRR** | Disable or relax VRR when applying a low background FPS limit (to avoid conflicts). | Not tied to background FPS in the same way. |

We have the main knobs (background FPS, no render/present in background); we don’t mirror SK’s exact power/VRR policy.

---

## 4. Other Special K behaviors we don’t have

- **Force windowed when Render in Background:** SK can force windowed mode if “Render in Background” is on (`dxgi_swapchain.cpp`), so fullscreen exclusive doesn’t fight background rendering. We don’t force windowed.
- **SDL backend:** SK **does not** spoof GetForegroundWindow for SDL games (`! rb.windows.sdl`), to avoid breaking SDL focus handling. We don’t detect SDL.
- **Process priority:** SK can raise process priority when “Render In Background” is enabled (`config.priority.raise_bg`). We don’t change process priority.
- **XInput when background:** SK keeps **XInput enabled** when `SK_IsGameWindowActive() || game_window.wantBackgroundRender()` so the gamepad keeps working in background. We don’t explicitly keep XInput enabled for continue-rendering; We now match this: when Continue Rendering is on, ShouldBlockGamepadInput() returns false so we never zero XInput in background; we also rewrite GetRawInputBuffer so each RAWINPUT item's header.wParam is RIM_INPUT (gamepad/HID not seen as background).
- **ShowCursor:** SK forces cursor visible when background_render and not fullscreen. We don’t.
- **Game-specific overrides:** SK’s `wantBackgroundRender()` can be false for certain games (e.g. Hello Kitty) even if config is true. We have a single setting.

---

## 5. Raw input (WM_INPUT / GetRawInputData)

Games can detect “in background” from raw input: **WM_INPUT** `wParam` is **RIM_INPUT** (foreground) or **RIM_INPUTSINK** (background), and **GetRawInputData** returns a **RAWINPUT** whose **header.wParam** has the same value. If we only spoof the message `wParam` in GetMessage/PeekMessage, the game can still call **GetRawInputData** and read **RIM_INPUTSINK** from the buffer and block input.

| What | Display Commander |
|------|-------------------|
| **GetMessage/PeekMessage** | When continue_rendering and `msg.message == WM_INPUT` and `msg.wParam == RIM_INPUTSINK`, we rewrite `msg.wParam = RIM_INPUT`. |
| **GetRawInputData** | When continue_rendering and the returned RAWINPUT has `header.wParam == RIM_INPUTSINK`, we rewrite `header.wParam = RIM_INPUT` in the output buffer so the game does not see “background” when it reads the data. |

Special K does the same: it rewrites WM_INPUT in the message loop and in **GetRawInputData_Detour** rewrites the RAWINPUT header when returning data (see `external-src/SpecialK/src/input/raw_input.cpp`).

---

## 6. What we have that matches or is close

- **GetFocus / GetForegroundWindow / GetGUIThreadInfo** spoofing when continue rendering is on.
- **GetActiveWindow** spoofing (SK has this off; we have it on).
- **WM_ACTIVATE, WM_KILLFOCUS, WM_ACTIVATEAPP, WM_NCACTIVATE** handling (suppress or fake so game thinks it’s active).
- **WM_MOUSEACTIVATE** handling (we suppress; SK returns MA_* and calls ActivateWindow).
- **WM_SHOWWINDOW** hide suppression.
- **Prevent minimize** (ShowWindow, WM_SYSCOMMAND SC_MINIMIZE, WM_WINDOWPOSCHANGING/CHANGED) when continue rendering or prevent-minimize.
- **Background FPS** and **no render / no present in background**.
- **Fake activation** (PostMessage WM_ACTIVATE, WM_SETFOCUS, WM_ACTIVATEAPP, WM_NCACTIVATE) when we need to reinforce “active” state.

---

## 7. Summary: what we’re missing from “Continue Rendering”

1. **GetFocus thread/process check:** Only spoof for the **game window’s thread** (and possibly return 0 for other processes). Optional for ReShade; improves parity with SK.
2. **GetForegroundWindow:** Consider **not** spoofing for true fullscreen and, if feasible, for SDL-backed games.
3. **Message handling:** Optionally call **DefWindowProc** for deactivation messages then inject **WM_KILLFOCUS** and block (like SK) so games that mute audio on WM_ACTIVATEAPP still think they’re active without seeing the deactivate.
4. **WM_MOUSEACTIVATE:** If our hook can return **MA_ACTIVATE** / **MA_ACTIVATEANDEAT** instead of only suppressing, do that and call **ActivateWindow** for consistency with SK.
5. **Force windowed** when Continue Rendering is enabled (to avoid fullscreen-exclusive vs background rendering issues).
6. **Process priority** for background process when Continue Rendering is on (optional).
7. **XInput:** Ensure we don’t disable XInput when app is in background and continue rendering is on.
8. **Optional:** Game-specific disable of continue rendering (like SK’s per-game overrides).

Implementing (1)–(4) would bring our behavior closest to Special K’s “Render in Background” feature; (5)–(8) are optional improvements.

---

## 8. Other APIs games may use to detect “in foreground”

If the game still detects background **even with “Debug: Suppress all GetMessage/PeekMessage” on**, it is not relying on the message loop for that check. It is likely using one or more of the following.

| API | What it does | We spoof? |
|-----|----------------|-----------|
| **GetForegroundWindow()** | Returns HWND of foreground window. Game compares to its window. | Yes (return game window when continue_rendering). |
| **GetFocus()** | Returns focus HWND for calling thread. | Yes. |
| **GetActiveWindow()** | Returns active window for thread. | Yes. |
| **GetGUIThreadInfo()** | Returns hwndActive, hwndFocus, etc. | Yes (for game thread / idThread 0). |
| **IsIconic(hwnd)** | TRUE if window is minimized. Game may treat “minimized” as background. | **No.** Consider spoofing to FALSE for the game window when continue_rendering. |
| **IsWindowVisible(hwnd)** | Visibility flag. Can be used together with other checks. | **No.** Optional: spoof to TRUE for game window when continue_rendering. |
| **NtUserGetMessage / NtUserPeekMessage** | win32u syscalls; user32 GetMessage/PeekMessage call these. If the game or its engine calls **NtUser*** directly, our user32 hooks are bypassed. | **No.** Would require hooking win32u.dll (and dealing with syscall semantics). Special K uses NtUserPeekMessage in some paths. |
| **GetRawInputData** | RAWINPUT header.wParam has RIM_INPUT vs RIM_INPUTSINK. | Yes (rewrite to RIM_INPUT when continue_rendering). |
| **GetRawInputBuffer** | Batch of RAWINPUTs; each has header.wParam. | Check: we hook it but may not rewrite RIM_INPUTSINK → RIM_INPUT in every item. |
| **Window proc (WM_ACTIVATE, WM_ACTIVATEAPP, etc.)** | Delivered to the window we subclass. If the game has another window (e.g. launcher) that receives activation messages, we might not hook it. | We hook the swapchain/game window. |
| **SDL_GetWindowFlags** (SDL_WINDOW_INPUT_FOCUS, etc.) | If the game uses SDL, it may use SDL focus flags. | No (no SDL hooks). |
| **DirectX / DXGI** | Some titles infer state from fullscreen/present. | Not spoofed as “foreground”. |

**Practical next steps:** Add spoofing for **IsIconic** (and optionally **IsWindowVisible**) for the game window when Continue Rendering is on, so the game does not see “minimized” or “not visible”. If the game uses **NtUserPeekMessage**/ **NtUserGetMessage** directly, only hooking win32u would fix that (higher effort).

**Gamepad still blocked in background:** If the game still blocks gamepad while we spoof focus/visibility, it may be using (1) an API we don't hook (e.g. **Windows.Gaming.Input** or another input API that checks focus internally), (2) **NtUser\*** message APIs so it still sees deactivation, or (3) a different window/thread for the "main" window. Use **Window Info → Continue Rendering & Input Blocking → Continue Rendering API debug** to see which APIs were last called, what they returned, and whether we overrode; if e.g. GetForegroundWindow is never called or not overridden, the game may be using another method to detect background.
