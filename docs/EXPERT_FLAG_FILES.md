# Expert: Flag files in the game directory

Display Commander reads optional **flag files** in the **game executable directory** (the folder containing the game’s `.exe`). These allow expert users to enable debugging, standalone mode, or load-order behavior without changing config from the UI.

**Location**: Same folder as the game executable (e.g. `C:\Games\MyGame\`). Not the ReShade addon folder.

---

## Exact filename flags

These are checked by **exact filename** (no variants).

| File | Effect |
|------|--------|
| **`.GET_PROC_ADDRESS`** | When this file exists, the addon logs every successful `GetProcAddress` (any module, non-null result) to `DisplayCommander.log`, **once per (module path, symbol)**. Useful to see which DLL exports are actually resolved by the game or other loaders. Check is done once at first use; remove the file and restart the game to disable. |

**Where to put the file:** In the **same folder as the game .exe** (the process executable). The addon checks that directory once and logs either `GetProcAddress logging (.GET_PROC_ADDRESS): enabled (exe dir: ...)` or `disabled - file not found (exe dir: ...)` so you can confirm which path was used. If you see "disabled - file not found", place the file in the reported exe dir.

Log lines for each resolution look like: `GetProcAddress: C:\...\some.dll -> ExportName (caller: C:\...\caller.dll)` (or `-> ordinal 42` for ordinal lookups). Each line is logged once per (module, symbol, caller) so the same export resolved from different callers appears separately.

---

## Segment-based flags (`.NAME` or `.NAME.anything`)

These are detected by **filename segment**: any file whose name starts with `.` and has the given segment as the second part (case-insensitive) is recognized. For example `.NO_RESHADE`, `.NO_RESHADE.off`, or `.no_reshade` all match the `NO_RESHADE` / `NORESHADE` flag.

| Segment(s) | Effect |
|------------|--------|
| **`.NO_RESHADE`** / **`.NORESHADE`** | ReShade is not loaded; Display Commander runs in standalone mode and opens a simplified settings window (FPS limiter, mute, target display). Use when you want DC without ReShade (e.g. SetupDC). |
| **`.NODC`** | ReShade is loaded as usual, but Display Commander does not register as an addon (proxy-only: DC DLL can still act as dxgi.dll / d3d11.dll etc., but no addon UI). |
| **`.UI`** | Independent settings window opens at start (when using the ReShade path). |
| **`.NO_EXIT`** / **`.NOEXIT`** | When the game tries to exit (ExitProcess, WM_CLOSE, WM_QUIT, etc.), the addon blocks the exit and opens the independent UI instead. For **debugging** only (e.g. inspect state before exit). |

---

## Summary

- **Game exe directory** = folder containing the game `.exe`.
- **Exact-name**: `.GET_PROC_ADDRESS` → log all successful GetProcAddress resolutions (once per module+symbol).
- **Segment-based**: `.NO_RESHADE` / `.NORESHADE`, `.NODC`, `.UI`, `.NO_EXIT` / `.NOEXIT` → standalone / proxy-only / open UI / block exit.

Remove the flag file and restart the game to revert behavior.
