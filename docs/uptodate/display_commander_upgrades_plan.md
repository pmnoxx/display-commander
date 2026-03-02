# Display Commander Upgrades Plan

## Status: **Implemented**

## Overview

Unify how Display Commander is selected and loaded with a **selector mode** (local / global / debug / stable) and separate version lists for **debug** vs **stable**, similar in spirit to the [ReShade load path design](../reshade_load_path_design.md) but with different folder layout and sources.

**References:**
- [Latest Debug Build](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug) – debug builds with PDBs, updated on every push to main.
- [Stable releases](https://github.com/pmnoxx/display-commander/releases/tag/v0.11.0) – versioned tags (e.g. v0.11.0 → 0.11.0).

---

## 1. Selector mode and folders

### 1.1 Mode enum

| Mode     | Meaning | Source / path |
|----------|--------|----------------|
| **local**  | Injection DLL (default) | The proxy/game-folder copy; loader does not load from central, runs as proxy or from game dir. |
| **global** | Installed in central root | `%LocalAppData%\Programs\Display_Commander\` (addon in base path, not in a version subfolder). |
| **debug**  | Debug builds | `%LocalAppData%\Programs\Display_Commander\Debug\X.Y.Z` – from [latest_debug](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug) and any previously downloaded debug builds. |
| **stable** | Stable releases | `%LocalAppData%\Programs\Display_Commander\Dll\X.Y.Z` (or `Release\X.Y.Z`) – from GitHub release tags (e.g. v0.11.0, v0.12.199). |

**Base path:** `%LocalAppData%\Programs\Display_Commander` (same as current `GetDownloadDirectory()` / `GetLocalDcDirectory()`).

- **Debug folder:** `Base\Debug\` – new; only debug builds (latest_debug → `Debug\X.Y.Z` by version from DLL).
- **Stable folder:** `Base\Dll\` – existing; keep for stable tagged releases (v0.11.0 → `Dll\0.11.0`). Option: rename to `Release\` for clarity; this plan uses **Dll** for stable to minimise churn.

### 1.2 Config variables

| Variable | Values | Description |
|----------|--------|-------------|
| **dc_selector_mode** | `"local"` \| `"global"` \| `"debug"` \| `"stable"` | Which source to run. Default: `"local"`. |
| **dc_version_for_debug** | `"latest"` \| `X.Y.Z` | When mode is **debug**: which version in `Debug\` to load. |
| **dc_version_for_stable** | `"latest"` \| `X.Y.Z` | When mode is **stable**: which version in `Dll\` to load. |

Migration: current `DcSelectedVersion` (`""` \| `"latest"` \| `X.Y.Z`) maps to **stable** mode with the same version semantics; `""` → global or stable "no override" (see fallback below).

---

## 2. Version lists in UI

### 2.1 When **debug** is selected

- **Version list:** `"latest"` + all versions present in `Base\Debug\` (subdirs `X.Y.Z` that contain DC addon DLLs), sorted descending.
- **Download selector:** "Download debug build" – fetches [latest_debug](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug), extracts version from DLLs, installs to `Debug\X.Y.Z`. Optionally: "Download another debug version" if we ever support multiple debug artifacts (for now, one "latest" debug download is enough).

### 2.2 When **stable** is selected

- **Version list:** `"latest"` + all versions present in `Base\Dll\` (subdirs `X.Y.Z` with DC addon DLLs), sorted descending.
- **Download selector:** "Download version" – list of **available** stable releases (from GitHub: [FetchDisplayCommanderReleasesFromGitHub](https://github.com/pmnoxx/display-commander) or tags), then "Download selected" to install into `Dll\X.Y.Z`. Existing "Download another version" behaviour can be reused and driven by this list.

### 2.3 Refresh

- **One-shot refresh:** Button or action "Refresh" that (1) rescans `Debug\` and `Dll\` for installed versions, and (2) optionally re-fetches available stable tags from GitHub so the "download another version" list is up to date.

---

## 3. Resolve directory for loading

### 3.1 GetDcDirectoryForLoading() semantics

Given current config (`dc_selector_mode`, `dc_version_for_debug`, `dc_version_for_stable`):

| Mode    | Resolved path |
|---------|----------------|
| **local**  | Not used for central load – loader uses "run from proxy/game dir" and does not load from base. Return empty or a sentinel; loader branch is "if local, don’t load from central". |
| **global** | `Base` (root) if it contains a DC addon; else fallback (see below). |
| **debug**  | If `dc_version_for_debug == "latest"` → highest version in `Base\Debug\`; else `Base\Debug\X.Y.Z`. If selected version missing → fallback. |
| **stable** | If `dc_version_for_stable == "latest"` → highest version in `Base\Dll\`; else `Base\Dll\X.Y.Z`. If selected version missing → fallback. |

**Fallback (when selected version or folder missing):** e.g. fall back to **global** (base path), or to "latest" in the same folder, or to base. Exact rule to define (e.g. debug missing → global; stable missing → global).

### 3.2 Loader behaviour (main_entry / proxy)

- If **local**: do not load DC from central path; run as the injection/proxy instance (current "load from game dir" behaviour).
- If **global** / **debug** / **stable**: resolve path via `GetDcDirectoryForLoading()`, load addon from that directory (existing loader path).

---

## 4. Data structures and API (conceptual)

### 4.1 Location type (optional, for consistency with ReShade)

```text
DcLocationType: Local | Global | DebugVersion | StableVersion
```

- **Local** – proxy/game copy (no path from base).
- **Global** – base path when it contains addon.
- **DebugVersion** – `Base\Debug\X.Y.Z`.
- **StableVersion** – `Base\Dll\X.Y.Z`.

### 4.2 Functions to introduce or extend

- **GetDcLocations()** – returns list of (type, version, directory) for Global + all `Debug\*` + all `Dll\*` (and optionally Local with a given game dir). Omit Local if not needed for "list".
- **ChooseDcDirectory(mode, version_debug, version_stable)** – returns one directory (and fallback info if needed). Handles "latest", missing version, and fallback to global.
- **GetDcDirectoryForLoading()** – reads config, calls ChooseDcDirectory, returns path. When mode is **local**, return empty (or a sentinel) so loader does not load from central.

### 4.3 Version lists for UI

- **GetDcInstalledVersionListDebug(size_t* out_count)** – versions in `Base\Debug\`, sorted desc (for version combo when mode is debug).
- **GetDcInstalledVersionListStable(size_t* out_count)** – versions in `Base\Dll\`, sorted desc (for version combo when mode is stable). Can alias or replace current `GetDcInstalledVersionList` which already scans `Dll\`.
- **GetDcAvailableStableReleases()** – from GitHub (existing fetch), for "download another version" when stable is selected.
- Debug: "Download latest debug" → existing `DownloadDcLatestDebugToDll` but target **Debug** folder instead of Dll (new helper or parameter).

---

## 5. Download and install

### 5.1 Debug

- **Source:** [latest_debug](https://github.com/pmnoxx/display-commander/releases/tag/latest_debug).
- **Install to:** `Base\Debug\X.Y.Z` (X.Y.Z from version in downloaded addon DLLs). New function e.g. `DownloadDcLatestDebugToDebugFolder(out_error)` or extend existing to take target subfolder (`Dll` vs `Debug`).

### 5.2 Stable

- **Source:** Release by tag (e.g. v0.11.0) – [FetchReleaseByTag](https://github.com/pmnoxx/display-commander) / `DownloadDcVersionToDll(version)`.
- **Install to:** `Base\Dll\X.Y.Z`. Already exists; ensure version list used in UI is the **stable** list (Dll only).

### 5.3 Set as global

- **Action:** Copy the currently selected (or running) DC addon files into **base path** so that "global" mode uses that version. One-time copy; overwrite addon in base. Implement e.g. "Set current as global" button that copies from `GetDcDirectoryForLoading()` (or running addon path) to base.

---

## 6. UI flow

1. **Mode selector:** Combo or radio: Local (injection DLL) | Global | Debug | Stable. Persist `dc_selector_mode`.
2. **When Debug:** Version combo = "Latest" + installed in `Debug\`. Download = "Download latest debug" (and optional refresh). Persist `dc_version_for_debug`.
3. **When Stable:** Version combo = "Latest" + installed in `Dll\`. Download = "Download another version" (list from GitHub) + "Download selected". Persist `dc_version_for_stable`.
4. **Refresh:** Rescan `Debug\` and `Dll\`; optionally refetch GitHub stable list.
5. **Set as global:** Button to copy selected/current to base path.
6. **Open folder:** Links to base, or to `Debug\`, or to `Dll\` depending on mode (or show all).

---

## 7. Implementation order (suggested)

1. **Config** – Add `dc_selector_mode`, `dc_version_for_debug`, `dc_version_for_stable`; migration from `DcSelectedVersion`.
2. **Folders** – Introduce `Debug\`; keep `Dll\` for stable. Ensure no cross-use (debug builds only in Debug, stable only in Dll).
3. **Resolve logic** – Implement or extend `GetDcDirectoryForLoading()` with mode + version_debug/version_stable and fallbacks (local → no central load; global → base; debug → Debug\X.Y.Z or latest; stable → Dll\X.Y.Z or latest).
4. **Loader** – In main_entry (and proxy path), if mode is **local**, skip loading from central; else use `GetDcDirectoryForLoading()`.
5. **Download debug to Debug** – New or extended download that writes to `Base\Debug\X.Y.Z`.
6. **Version lists** – `GetDcInstalledVersionListDebug`, keep/rename `GetDcInstalledVersionList` for stable; wire UI to mode.
7. **UI** – Mode selector, version combos per mode, download buttons, refresh, "Set as global".

---

## 8. Edge cases and fallbacks

- **local** and no proxy/game copy: N/A (user chose local; we run as that instance).
- **global** and base has no addon: Fallback to e.g. latest in Dll, or show warning "No addon in global folder" and do not load (or keep previous behaviour: load from Dll if present).
- **debug** and "latest" but `Debug\` empty: Fallback to global, or prompt "Download latest debug".
- **debug** and X.Y.Z but that folder missing: Fallback to latest in Debug, or to global; optionally set fallback_* for UI.
- **stable** and "latest" but `Dll\` empty: Fallback to global.
- **stable** and X.Y.Z but that folder missing: Fallback to latest in Dll, or global; set fallback_* for UI.

Define one consistent fallback chain (e.g. selected version → latest in same folder → global → base path if has addon) and document in code.

---

## 9. File / module summary

| Area | Files / changes |
|------|------------------|
| Config | `display_commander_config` (or existing DC section): add dc_selector_mode, dc_version_for_debug, dc_version_for_stable; migration from DcSelectedVersion. |
| DC load path | `utils/dc_load_path.hpp`, `utils/dc_load_path.cpp`: GetDcDirectoryForLoading() from mode + version; optional GetDcLocations, ChooseDcDirectory; GetDcInstalledVersionListDebug, GetDcInstalledVersionListStable. |
| Download | `utils/version_check.cpp`: Download debug to `Debug\X.Y.Z` (new or extend DownloadDcLatestDebugToDll to take target folder). Stable download already to Dll. |
| Loader | `main_entry.cpp`: If dc_selector_mode == local, do not load from central; else GetDcDirectoryForLoading() and load from that path. |
| UI | `ui/new_ui/main_new_tab.cpp` (or equivalent): Mode selector, version combos per mode, download/refresh/set-as-global. |

---

## 10. Summary

- **Modes:** local (injection DLL, default) | global (base path) | debug (Debug\X.Y.Z) | stable (Dll\X.Y.Z).
- **Config:** dc_selector_mode, dc_version_for_debug, dc_version_for_stable.
- **Lists:** Debug mode → "latest" + Debug\*; Stable mode → "latest" + Dll\*.
- **Download:** Debug → latest_debug to Debug\X.Y.Z; Stable → release by tag to Dll\X.Y.Z.
- **Actions:** Refresh (rescan + optional GitHub fetch), Set as global (copy to base).
- **Resolve and fallback:** One function (e.g. ChooseDcDirectory) with clear fallback chain; GetDcDirectoryForLoading() uses it; loader treats local as "no central load".
