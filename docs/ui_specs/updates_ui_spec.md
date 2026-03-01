# UI Spec: Updates (ReShade & Display Commander)

This document defines the UI and behavior for the **Updates** feature: ReShade version selection/download and Display Commander version selection/download, plus how loading works.

---

## Updates UI

**Placement:** The Updates UI lives in the **main tab**. It replaces the existing updates block there; implement this design in place of the current one.

### ReShade

- **Checkbox** (default off): **"Prefer global ReShade"**
  - Tooltip: shows `local version: ...`, `global version: ...`; explains that by default we prefer local > global, but this checkbox changes preference to global > local.
- **ReShade newest version available:** `...`
  - **Green** when up to date: the **loaded** version (local or global, per preference) is ≥ newest available to download. **Red** when a newer version exists: newest available > loaded version. We check GitHub and reshade.me for “newest available”. Version comparison is numerical triple: `(x1,y1,z1) > (x2,y2,z2)`.
- **Download subsection**
  - A selector to choose the version to download. The list shows **only remote versions** (from GitHub + reshade.me), not already-installed versions.
  - **Download** button: overwrites the DLLs in the single global folder `%LocalAppData%\Programs\Display_Commander\Reshade`, so global is always one version.
- **Open folder:** One button that opens the ReShade global folder. Tooltip shows the **full path** (e.g. `C:\Users\...\AppData\Local\Programs\Display_Commander\Reshade`) so the user can copy or verify it; they can manually copy files there to revert or manage versions.

### Display Commander

- **Checkbox** (default off): **"Prefer global DC"**
  - Tooltip: shows local DC version and global DC version; explains that by default we prefer local > global, but this checkbox changes preference to global > local for Display Commander.
- **Newest stable available:** `...`
  **Newest debug version available:** `...`
- **Download subsection**
  - A selector to choose either **debug builds** or **stable builds**.
  - A selection (based on above) with available versions.
  - **Download** button (overrides that version).
- **Open folder:** One button that opens the Display Commander global folder. Tooltip shows the **full path** (e.g. `C:\Users\...\AppData\Local\Programs\Display_Commander`) so the user can copy or verify it; they can manually copy files there to revert or manage versions.

**Note:** We do not offer loading a specific version from `DLLS\X.Y.Z` directly in the UI (keeps things simple). The user can open the folder manually if they want to copy or revert files themselves.

---

## Up-to-date status (version comparison)

- **Loaded version** = the version we are actually using (from local or global, per preference).
- **Newest available** = newest version we can download (ReShade: from GitHub and reshade.me; DC: stable or debug from GitHub).
- **Green (up to date):** loaded version ≥ newest available. **Red (newer exists):** newest available > loaded version.
- Comparison is numerical on the version triple: `(x1, y1, z1) > (x2, y2, z2)` (e.g. 6.7.3 vs 6.7.2).

---

## Loading semantics (two ways only)

Loading works in exactly two ways:

1. **Local folder**
   - **ReShade:** local = `reshade64.dll` (in game/process directory).
   - **Display Commander:** local = the proxy `dc64.dll` if it exists, or the addon itself (injection case).
   - We create a copy of the DLL before loading it so the file on disk can be updated (on Windows a loaded library cannot be overwritten). The copy goes to a **temporary folder** (see below).

2. **Global folder**
   For both ReShade and DC, global load uses the same flow:
   1. **Check version** X.Y.Z of the global file (the DLL in the global path).
   2. **If the X.Y.Z subfolder already exists** → load the library from that folder (no copy). On Windows multiple processes may load the same DLL; we never update a DLL with fixed version X.Y.Z, so the versioned folder is stable.
   3. **Otherwise** → copy the global file(s) into the X.Y.Z subfolder, then load from there. The global root is not locked; another process can update or replace the global copy while we run from the versioned copy.
   - **ReShade:** Global path `%LocalAppData%\Programs\Display_Commander\Reshade`; to keep the Reshade folder clean we use versioned subfolder `...\Reshade\DLLS\X.Y.Z`. Download overwrites the DLLs in the global root; load uses `...\Reshade\DLLS\X.Y.Z` as above.
   - **Display Commander:** Global path `%LocalAppData%\Programs\Display_Commander` (current root); versioned subfolder `...\Display_Commander\DLLS\X.Y.Z` for both debug and release. When `DLLS\X.Y.Z` does not exist we **copy from the current root** `...\Display_Commander\` into that subfolder, then load. Same flow: check version → load from `DLLS\X.Y.Z` if present, else copy from root to `DLLS\X.Y.Z` then load. (We do not offer loading from `DLLS\X.Y.Z` directly in the UI; user can open the folder manually to copy/revert.)

### Copy-before-load: temporary location (local only)

For **local** copy-before-load only, the copy is placed in a temporary folder. Allowed options:

- **Windows temp:** e.g. `GetTempPath()` with a DC-specific subfolder (e.g. `Display_Commander_Load\<pid>_<timestamp>\` or similar) so we can identify and clean our copies; or
- **Dedicated tmp under our app data:** e.g. `%LocalAppData%\Programs\Display_Commander\Temp\` (or `LoadCopy\`) so we control the location and can clean on process exit.

Implementation may choose one. Copies should be cleaned when possible (e.g. on process exit) to avoid leaving stale DLLs.

---

## References

- Task / redesign: `docs/tasks/reshade_dc_updates_redesign.md`
- ReShade load path: `docs/reshade_load_path_design.md`
- DC upgrades plan: `docs/tasks/display_commander_upgrades_plan.md`
