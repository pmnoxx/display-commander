# ReShade & Display Commander Updates – Redesign (Task)

## Status: **Planned**

## Overview

Redesign the ReShade and Display Commander update/version feature so that:

1. The UI matches the spec below (and `docs/ui_specs/updates_ui_spec.md`).
2. Loading has exactly two modes: **local folder** or **global folder**, with clear semantics (including copy-before-load for global so the global copy is not locked).

This task doc keeps the **UI design** here so we can extrapolate how the feature should work; the canonical UI spec is also in `docs/ui_specs/updates_ui_spec.md`.

---

## UI design (implementation reference)

### Updates

**Placement:** Main tab; replace the existing updates UI with this design. Implementation target: `main_new_tab.cpp` — `DrawUpdatesSectionContent()` (and the content inside the "Updates" collapsing header around line 2162) is replaced by the new ReShade + Display Commander sections per the spec.

#### ReShade

- **Checkbox** (default off): **"Prefer global ReShade"**
  - Tooltip: shows `local version: ...`, `global version: ...`; explains that by default we prefer local > global, but this checkbox changes preference to global > local.
- **ReShade newest version available:** `...`
  - Green when up to date; red when a newer version exists (we check GitHub and reshade.me).
- **Download subsection**
  - A selector to choose the version to download. The list shows **only remote versions** (from GitHub + reshade.me), not already-installed.
  - **Download** button: overwrites the DLLs in the single global folder `%LocalAppData%\Programs\Display_Commander\Reshade`, so global is always one version.
- **Open folder:** One button; opens the ReShade global folder `%LocalAppData%\Programs\Display_Commander\Reshade`.

#### Display Commander

- **Checkbox** (default off): **"Prefer global DC"**  
  - Tooltip: shows local DC version and global DC version; explains that by default we prefer local > global, but this checkbox changes preference to global > local for Display Commander.
- **Newest stable available:** `...`  
  **Newest debug version available:** `...`
- **Download subsection**
  - A selector to choose either **debug builds** or **stable builds**.
  - A selection (based on above) with **available versions**.
  - **Download** button (overrides that version).
- **Open folder:** One button; opens the Display Commander global folder `%LocalAppData%\Programs\Display_Commander`.

We do not offer loading from `DLLS\X.Y.Z` directly in the UI (keeps things simple); user can open the folder manually to copy/revert.

---

## Loading semantics (two ways only)

Loading works in exactly two ways:

1. **Local folder**
   - **ReShade:** local = `reshade64.dll` (or `Reshade32.dll`) in the game/process directory.
   - **Display Commander:** local = the proxy `dc64.dll` if it exists, or the addon itself (injection case).
   - **Copy before load:** We create a copy of the DLL before loading it so the file on disk can be updated (on Windows a loaded library cannot be overwritten). The copy goes to a temporary folder (Windows temp with a DC-specific subfolder, or a dedicated tmp under `%LocalAppData%\Programs\Display_Commander\`; see UI spec).

2. **Global folder**
   For both ReShade and DC: (1) Check version X.Y.Z of the global file. (2) If the X.Y.Z subfolder already exists → load from that folder (no copy; on Windows multiple processes may load the same DLL; we never update a fixed-version X.Y.Z folder). (3) Otherwise → copy the global file(s) to the X.Y.Z subfolder, then load from there. The global root is not locked.
   - **ReShade:** Global path `%LocalAppData%\Programs\Display_Commander\Reshade`; versioned subfolder `...\Reshade\DLLS\X.Y.Z` (keeps Reshade folder clean). Download overwrites the global root; load uses `...\Reshade\DLLS\X.Y.Z` as above.
   - **Display Commander:** Global path `%LocalAppData%\Programs\Display_Commander` (current root). When `DLLS\X.Y.Z` does not exist we copy from the current root into `...\Display_Commander\DLLS\X.Y.Z`, then load. Same flow: check version → load from `DLLS\X.Y.Z` if present, else copy from root to that subfolder then load.

From this we can extrapolate:

- **Prefer local / prefer global** drives which of the two sources is chosen when both exist (local folder vs global-derived `X.Y.Z`).
- **Newest version available** (ReShade: GitHub + reshade.me; DC: stable + debug) drives the “green/red” status and the download selector.
- **Download** always targets the “global” side: ReShade → global ReShade version (or a chosen version in global); DC → chosen debug/stable version in `...\Display_Commander\DLLS\X.Y.Z`).

---

## Relation to existing docs

| Doc | Role |
|-----|------|
| `docs/ui_specs/updates_ui_spec.md` | Canonical UI spec for Updates. |
| `docs/reshade_load_path_design.md` | ReShade location list and `ChooseReshadeVersion`; align with “local vs global” and copy-to-`X.Y.Z`-then-load. |
| `docs/RESHADE_LOAD_SOURCE_DESIGN.md` | Legacy load source (Local / Shared / Specific version); redesign converges to “local folder” vs “global folder” only. |
| `docs/tasks/display_commander_upgrades_plan.md` | DC selector mode and Debug/Stable; align DC UI and loading with “local vs global” and the Download subsection above. |

---

## Implementation notes (to be refined)

- **Config:** One “prefer global” flag per product (ReShade, DC); version lists and “newest available” from GitHub/reshade.me; persist in Display Commander config.
- **ReShade:** Unify with `GetReshadeLocations` / `ChooseReshadeVersion` and single load path; "local" = game dir with `reshade64.dll`; "global" = check version → if `...\Reshade\DLLS\X.Y.Z` exists load from there, else copy global to DLLS\X.Y.Z then load (keeps Reshade folder clean).
- **DC:** Same idea: local = proxy/game copy; global = check version → if `...\Display_Commander\DLLS\X.Y.Z` exists load from there, else copy from current root to that subfolder then load (both debug and release use DLLS\X.Y.Z; we do not offer loading from DLLS directly in UI; open folder lets user copy/revert manually).
- **Copy-before-load (local only):** For local load, copy to temp folder so the source file can be updated on disk. For global load we use versioned subfolder X.Y.Z (no temp).

---

## Summary

- **UI:** ReShade and DC each get “Prefer global” checkbox, newest-version status (green/red), and a Download subsection with version selector and Download button (overwriting/defining that global version).
- **Loading:** Only **local folder** or **global folder**; for global we check version → load from X.Y.Z if present, else copy global to X.Y.Z then load (global root not locked; multiple processes may share same X.Y.Z DLL).
