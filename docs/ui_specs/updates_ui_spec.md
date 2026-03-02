# UI Spec: Updates (ReShade & Display Commander)

This document defines the UI and behavior for the **Updates** feature: ReShade version selection/download and Display Commander version selection/download, plus how loading works.

---

## Updates UI

**Placement:** The Updates UI lives in the **main tab**. It replaces the existing updates block there; implement this design in place of the current one.

### ReShade

- **Preference:** local Reshade64.dll / Reshade32.dll (game folder) then global Reshade64.dll / Reshade32.dll.
- **Checkbox** (default off): **"Prefer global ReShade"**
  - Inline: show **Local version** and **Global version** directly under the checkbox.
  - Tooltip: explains that by default we prefer local > global, but this checkbox changes preference to global > local.
- **ReShade newest version available:** `...`
  - **Green** when up to date: the **loaded** version (local or global, per preference) is ≥ newest available to download. **Red** when a newer version exists: newest available > loaded version. We check GitHub and reshade.me for “newest available”. Version comparison is numerical triple: `(x1,y1,z1) > (x2,y2,z2)`.
  - **Tooltip on status ("Up to date" / "Newer exists"):** Show the source of the version information, e.g. *"Version info from: GitHub (crosire/reshade), reshade.me"*, so the user knows where "newest available" comes from.
- **Download subsection**
  - A selector to choose the version to download. The list shows **only remote versions** (from GitHub + reshade.me), not already-installed versions.
  - **Download** button: overwrites the DLLs in the single global folder `%LocalAppData%\Programs\Display_Commander\Reshade`, so global is always one version.
- **Open folder:** One button that opens the ReShade global folder. Tooltip shows the **full path** (e.g. `C:\Users\...\AppData\Local\Programs\Display_Commander\Reshade`) so the user can copy or verify it; they can manually copy files there to revert or manage versions.
- **Delete local ReShade:** Shown only when the game folder contains ReShade (Reshade64.dll / Reshade32.dll). Removes those DLLs from the game folder. Safe because we never load the game-folder copy directly (we copy to temp then load); next run will use global ReShade if preferred.

### Display Commander

- **Preference:** local zzz_display_commander.addon64 / zzz_display_commander.addon32, then global same, then proxy .dll (dxgi.dll / winmm.dll / d3d11.dll / whatever .dll the game loaded DC through). We load the addon file, not dc64.dll/dc32.dll.
- **Checkbox** (default off): **"Prefer global DC"**
  - Inline: show **Local DC version**, **Local Proxy DC version**, and **Global DC version** directly under the checkbox.
  - **Local DC version:** Shows the version in the game folder only when zzz_display_commander.addon64/.addon32 is present there; otherwise **None** with tooltip explaining the addon is missing.
  - **Local Proxy DC version:** Shows the version of the proxy DLL (e.g. dxgi.dll, winmm.dll) when loaded as a proxy; (none) if not applicable.
  - Tooltip on Global DC version: explains preference order—local zzz_display_commander.addon64/.addon32 > global > proxy .dll—and that the checkbox switches to global > local.
- **When "Prefer global DC" is on:** **"Use version"** combo to choose what to run:
  - **Stable (latest)** — use latest from `Dll\` (stable builds).
  - **Debug: Latest (highest in cache)** — use highest version in `Display_Commander\Debug\X.Y.Z`.
  - **Debug: Latest from GitHub (X.Y.Z)** — use the current latest debug version (number shown); after download it lives in `Display_Commander\Debug\X.Y.Z`.
  - **Debug: X.Y.Z (cached)** — use a specific version already in `Display_Commander\Debug\X.Y.Z`. This gives a choice of latest from GitHub (with version number) or any previously downloaded debug version from local cache, since GitHub only exposes one debug tag (`latest_debug`).
- **Newest stable available:** `...`
- **Newest debug version available:** `...`
  - **Tooltip on status ("Up to date" / "Newer exists")** for stable and for debug: Show the source of the version information (e.g. *"Version info from: GitHub (Display Commander stable releases)"* or *"Version info from: GitHub (Display Commander latest_debug)"*), so the user knows where "newest available" comes from.
- **Download subsection**
  - A selector to choose either **debug builds** or **stable builds**.
  - Stable: version list (remote) + **Download** (to `Dll\X.Y.Z`).
  - Debug:
    - Version selector includes **Latest from GitHub (X.Y.Z.W)** and **all locally cached debug versions** (from `Display_Commander\Debug\X.Y.Z`).
    - **Download** downloads only **Latest from GitHub** (since GitHub exposes only the `latest_debug` tag) and stores it in `Display_Commander\Debug\X.Y.Z`, building a local cache so "Use version" can offer cached choices.
  - **Download** button (overrides that version).
- **Open folder:** One button that opens the Display Commander global folder. Tooltip shows the **full path** (e.g. `C:\Users\...\AppData\Local\Programs\Display_Commander`) so the user can copy or verify it; they can manually copy files there to revert or manage versions.
- **Delete local DC:** Shown only when the game folder contains the DC addon (zzz_display_commander.addon64 / zzz_display_commander.addon32). Removes those files from the game folder. Next run will use global or proxy DC if preferred.

**Note:** We do not offer loading a specific version from `DLLS\X.Y.Z` directly in the UI (keeps things simple). The user can open the folder manually if they want to copy or revert files themselves.

---

## Up-to-date status (version comparison)

- **Loaded version** = the version we are actually using (from local or global, per preference).
- **Newest available** = newest version we can download (ReShade: from GitHub and reshade.me; DC: stable or debug from GitHub).
- **Green (up to date):** loaded version ≥ newest available. **Red (newer exists):** newest available > loaded version.
- **Source tooltip:** For both "Up to date" and "Newer exists", show a tooltip that states where "newest available" was obtained (ReShade: GitHub + reshade.me; DC stable: GitHub stable releases; DC debug: GitHub latest_debug). This lets users verify or understand the source of the information.
- Comparison is numerical on the version triple: `(x1, y1, z1) > (x2, y2, z2)` (e.g. 6.7.3 vs 6.7.2).

---

## Loading semantics (two ways only)

Loading works in exactly two ways:

1. **Local folder**
   - **ReShade:** preference local Reshade64.dll / Reshade32.dll (game folder) then global Reshade64.dll / Reshade32.dll.
   - **Display Commander:** when “Prefer global DC” is off, resolution order is: (1) local zzz_display_commander.addon64/.addon32 (e.g. game folder); (2) global same; (3) proxy .dll directory (dxgi.dll, winmm.dll, d3d11.dll, etc.). We load the addon file, not dc64.dll/dc32.dll. When the checkbox is on, global is preferred over local.
   - We create a copy of the DLL before loading it so the file on disk can be updated (on Windows a loaded library cannot be overwritten). The copy goes to a **temporary folder** (see below).

2. **Global folder**
   For both ReShade and DC, global load uses the same flow:
   1. **Check version** X.Y.Z of the global file (the DLL in the global path).
   2. **If the X.Y.Z subfolder already exists** → load the library from that folder (no copy). On Windows multiple processes may load the same DLL; we never update a DLL with fixed version X.Y.Z, so the versioned folder is stable.
   3. **Otherwise** → copy the global file(s) into the X.Y.Z subfolder, then load from there. The global root is not locked; another process can update or replace the global copy while we run from the versioned copy.
   - **ReShade:** Global path `%LocalAppData%\Programs\Display_Commander\Reshade`; to keep the Reshade folder clean we use versioned subfolder `...\Reshade\DLLS\X.Y.Z`. Download overwrites the DLLs in the global root; load uses `...\Reshade\DLLS\X.Y.Z` as above.
   - **Display Commander:** Global path `%LocalAppData%\Programs\Display_Commander` (current root); versioned subfolder `...\Display_Commander\DLLS\X.Y.Z` for both debug and release. When `DLLS\X.Y.Z` does not exist we **copy from the current root** `...\Display_Commander\` into that subfolder, then load. Same flow: check version → load from `DLLS\X.Y.Z` if present, else copy from root to `DLLS\X.Y.Z` then load. (We do not offer loading from `DLLS\X.Y.Z` directly in the UI; user can open the folder manually to copy/revert.)

### Hard link before load (when a copy would be used)

Where we would otherwise **copy** a DLL before loading (e.g. into a versioned folder or temp), we **try a hard link first** (no administrator rights required on the same NTFS volume). If the hard link fails (e.g. different volume, non-NTFS), we fall back to copying the file. This avoids duplicate disk usage and speeds the prepare step when source and destination are on the same volume.

### Copy-before-load: temporary location (local only)

For **local** copy-before-load only, the copy (or hard link) is placed in a temporary folder. Allowed options:

- **Windows temp:** e.g. `GetTempPath()` with a DC-specific subfolder (e.g. `Display_Commander_Load\<pid>_<timestamp>\` or similar) so we can identify and clean our copies; or
- **Dedicated tmp under our app data:** e.g. `%LocalAppData%\Programs\Display_Commander\Temp\` (or `LoadCopy\`) so we control the location and can clean on process exit.

Implementation may choose one. Copies/links should be cleaned when possible (e.g. on process exit) to avoid leaving stale DLLs.

---

## References

- Task / redesign: `docs/tasks/reshade_dc_updates_redesign.md`
- ReShade load path: `docs/reshade_load_path_design.md`
- DC upgrades plan: `docs/tasks/display_commander_upgrades_plan.md`
