# ReShade Load Path: Unified Location List & Selection Design

## Problem

1. **Override ignored when local ReShade exists**: ReShade is loaded in two steps: first `ProcessAttach_TryLoadReShadeFromCwd()` loads `Reshade64.dll` from the game folder (CWD); only if that fails does `ProcessAttach_TryLoadReShadeWhenNotLoaded()` use `GetReshadeDirectoryForLoading()`. So when the user selects a specific version (e.g. 6.7.3) but the game folder has 6.7.2, the local DLL is loaded and the override is never applied.

2. **Scattered logic**: “Where can ReShade come from?” and “Which one do we want?” are mixed across `reshade_load_path.cpp` and `main_entry.cpp`. There is no single list of all candidate locations.

## Goals

- **Single source of truth**: One function that returns all tracked ReShade locations with `(location_type, version, full_path)`, where **location_type** is an enum: **Local** | **Global** | **SpecificVersion**.
- **Explicit selection**: A second function that, given that list and the current setting, picks exactly one directory. All load paths use this result (no “try CWD first” then override).
- **Version handling**: Versions are normalized to `X.Y.Z` (strip a fourth component if present). Ordering is numerical on `(X, Y, Z)`.
- **Clear semantics** for settings: **local** | **latest** | **global** | **X.Y.Z** | **no**.

---

## 1. Data Structures

### 1.1 Location type enum

```cpp
enum class ReshadeLocationType {
    Local,           // game folder (Reshade32/64 in same folder as game exe)
    Global,           // one fixed location: ...\Display_Commander\Reshade (default when no local exists)
    SpecificVersion  // Reshade\Dll\X.Y.Z (versioned subfolders)
};
```

- **Local**: Only the game directory when it contains ReShade DLLs. At most one per list.
- **Global**: The single fixed base folder; default when no local exists. At most one per list.
- **SpecificVersion**: Each installed version under `Reshade\Dll\X.Y.Z`.

### 1.2 `ReshadeLocation`

One entry per directory that contains both `Reshade64.dll` and `Reshade32.dll` (or at least the one needed for the current process bitness; for simplicity we keep “both present” as the rule so the list is process-agnostic).

```cpp
struct ReshadeLocation {
    ReshadeLocationType type;   // Local | Global | SpecificVersion
    std::string version;        // normalized X.Y.Z (from DLL or folder name)
    std::filesystem::path directory;  // directory containing Reshade64.dll / Reshade32.dll
};
```

- **type**: Distinguishes Local (game folder), Global (base folder), and SpecificVersion (Dll\X.Y.Z) for selection logic.
- **version**: For game folder and base folder: read from `Reshade64.dll` (e.g. `GetDLLVersionString`), then normalized to X.Y.Z. For `Dll\X.Y.Z`: use the folder name (already X.Y.Z).
- **directory**: Full path to the folder; loader will use `directory / "Reshade64.dll"` or `directory / "Reshade32.dll"`.

### 1.3 Version normalization

- **Input**: Version string from DLL or UI (e.g. `"6.7.3"`, `"6.7.3.12345"`).
- **Output**: Exactly three components: `X.Y.Z`. If the string has a fourth component (e.g. `6.7.3.12345`), strip it to `6.7.3`.
- **Comparison**: Use existing `version_check::CompareVersions(a, b)` (already numerical). Ordering for “latest” is descending (highest first).
- **Place**: Add `NormalizeVersionToXyz(std::string)` in `version_check` (or reuse/extract the existing logic used for `VersionStringToXyzFolder` in `version_check.cpp`) and use it when building the list and when matching “specific version”.

---

## 2. API

### 2.1 Get all tracked locations

```cpp
std::vector<ReshadeLocation> GetReshadeLocations(const std::filesystem::path& game_directory);
```

**Responsibilities:**

- **Game folder (local)**  
  If `game_directory` contains both `Reshade64.dll` and `Reshade32.dll`, add one entry:  
  `{ type: ReshadeLocationType::Local, version: NormalizeVersionToXyz(GetDLLVersionString(game_directory / "Reshade64.dll")), directory: game_directory }`.  
  If version cannot be read, use empty string (such entries still participate for “local” and “latest” but not for “specific version” match).

- **Global base**  
  Base = `GetGlobalReshadeDirectory()` = `%LocalAppData%\Programs\Display_Commander\Reshade`.  
  If that directory contains both DLLs, add:  
  `{ type: ReshadeLocationType::Global, version: NormalizeVersionToXyz(GetDLLVersionString(base / "Reshade64.dll")), directory: base }`.

- **Versioned Dll folders**  
  For each subdirectory of `base / "Dll"` (e.g. `6.7.2`, `6.7.3`) that contains both DLLs, add:  
  `{ type: ReshadeLocationType::SpecificVersion, version: NormalizeVersionToXyz(subdir_name), directory: base / "Dll" / subdir_name }`.

**Return:** Vector of all such locations. Order is not required to be sorted; `ChooseReshadeVersion` will sort when needed.

**Edge cases:**

- `game_directory` empty: skip local entry.
- Duplicate paths (e.g. base same as a Dll subdir): acceptable to have two entries (base vs versioned); selection rules will pick one.
- Same path with different version representation: prefer one canonical representation (e.g. folder name for Dll\X.Y.Z, DLL for base/local).

### 2.2 Choose one directory from list and settings

```cpp
struct ChooseReshadeVersionResult {
    std::filesystem::path directory;   // empty if "no" or no valid location
    std::string fallback_selected;     // set when user chose X.Y.Z but we loaded another
    std::string fallback_loaded;       // version we actually used in that case
};

ChooseReshadeVersionResult ChooseReshadeVersion(
    const std::vector<ReshadeLocation>& locations,
    const std::string& selected_setting);
```

**selected_setting** (effective config value): `"no"` | `"local"` | `"latest"` | `"global"` | `"X.Y.Z"` (specific).

**Logic:**

| Setting    | Behavior |
|-----------|----------|
| `"no"`    | Return `directory = {}`, no fallback. |
| `"local"` | If any entry has `type == ReshadeLocationType::Local`, return its `directory`. Otherwise, return the directory of the entry with the **highest** version (same as “latest”). |
| `"latest"`| Sort `locations` by `version` descending (using `CompareVersions`). Return the first entry’s `directory`. If list empty, return empty. |
| `"global"` | Find the entry with `type == Global`. If found, return its `directory`. If not in list (e.g. no DLLs in base), fall back to highest versioned location; if no locations at all, return empty (do not use base path when DLLs are not there). |
| `"X.Y.Z"` | Normalize to `NormalizeVersionToXyz(selected_setting)`. Find an entry with `type == SpecificVersion` and `version == normalized`. If found, return that `directory`. If not found, **fallback**: set `fallback_selected = selected_setting`, `fallback_loaded = highest available version` (among SpecificVersion entries, or whole list), and return the directory of that highest version. |

**Sorting for “latest” and “local” fallback:** Sort by `version` using `version_check::CompareVersions` descending (so 11.10.9 > 10.15.20).

---

## 3. Integration

### 3.1 Config / UI

- **Current config**: Single key `ReshadeSelectedVersion`: `"global"`, `"latest"`, `"X.Y.Z"`, or `"no"`.
- **Addition**: Support `"local"` as a new option. Migration: if legacy `KEY_LOAD_SOURCE` was “use game folder” and no `KEY_SELECTED_VERSION`, we could map to `"local"` (to be aligned with existing migration in `GetReshadeSelectedVersionEffective`).

### 3.2 GetReshadeDirectoryForLoading

- **Signature**: Change to accept game directory:  
  `std::filesystem::path GetReshadeDirectoryForLoading(const std::filesystem::path& game_directory);`
- **Implementation**:  
  - If `GetReshadeSelectedVersionEffective() == "no"` → return `{}`.  
  - Otherwise:  
    - `locations = GetReshadeLocations(game_directory);`  
    - `result = ChooseReshadeVersion(locations, GetReshadeSelectedVersionEffective());`  
    - Update module-level fallback state from `result.fallback_*` (for UI).  
    - Return `result.directory`.

### 3.3 Loader flow (main_entry.cpp)

- **Remove** the “try CWD first, then override” behavior.
- **Single path**:
  1. `game_directory = ProcessAttach_GetConfigDirectoryW()` (executable directory).
  2. `path = GetReshadeDirectoryForLoading(game_directory)`.
  3. If `path.empty()`, do not load ReShade (respect “no”).
  4. Otherwise load `path / "Reshade64.dll"` or `path / "Reshade32.dll"` (full path) and set `g_reshade_module`.

So we **never** call `LoadLibraryA("Reshade64.dll")` from CWD when the user has chosen a different source; we always decide the directory from the list + setting, then load from that directory.

### 3.4 Fallback info for UI

- Keep `GetReshadeLoadFallbackVersionInfo(out_selected, out_loaded)`. It is set inside `ChooseReshadeVersion` / `GetReshadeDirectoryForLoading` when the user selected a specific version but we fell back to the highest installed version.

---

## 4. Version normalization details

- **Input**: e.g. `"6.7.3"`, `"6.7.3.12345"`, `"v6.7.3"`.
- **Steps**: (1) Optional: strip `v` prefix (e.g. via existing `ParseVersionString`). (2) If there are 4 or more dot-separated components, take the first three. Result must be `X.Y.Z` for comparison and for matching “specific version”.
- **Expose**: Either a public `NormalizeVersionToXyz` in `version_check` or a local helper in `reshade_load_path` that uses the same logic as `VersionStringToXyzFolder` in `version_check.cpp`.

---

## 5. File / responsibility summary

| File | Changes |
|------|--------|
| `utils/version_check.hpp/.cpp` | Expose (or add) `NormalizeVersionToXyz(std::string)` for X.Y.Z normalization. |
| `utils/reshade_load_path.hpp` | Add `ReshadeLocation`, `GetReshadeLocations(game_directory)`, `ChooseReshadeVersionResult`, `ChooseReshadeVersion(locations, setting)`. Declare `GetReshadeDirectoryForLoading(game_directory)`. |
| `utils/reshade_load_path.cpp` | Implement list building (local + base + Dll\X.Y.Z), `ChooseReshadeVersion` logic, and new `GetReshadeDirectoryForLoading(game_directory)` using list + choose. Keep fallback globals updated from `ChooseReshadeVersion` result. |
| `main_entry.cpp` | Get `game_directory` (e.g. `ProcessAttach_GetConfigDirectoryW()`), call `GetReshadeDirectoryForLoading(game_directory)`, load ReShade only from that path. Remove unconditional CWD load in `ProcessAttach_TryLoadReShadeFromCwd`; replace with single path load that respects the chosen directory. |
| UI (e.g. main_new_tab.cpp) | Add “Local” option if not present; ensure dropdown uses same config values (`"global"`, `"local"`, `"latest"`, `"X.Y.Z"`, `"no"`). |

---

## 6. Ordering and “latest” rule

- **Numerical ordering**: Use existing `version_check::CompareVersions`. For “latest”, sort so that higher version comes first (e.g. 11.10.9 before 10.15.20).
- **Stable tie-break**: If two entries have the same normalized version (e.g. base folder and a Dll\X.Y.Z with same version), prefer the one that matches the current mode (e.g. for “latest” prefer Dll\X.Y.Z over base, or vice versa—document once chosen). Simplest: prefer Dll\X.Y.Z over base when versions are equal.

---

## 7. Summary

- **One list**: `GetReshadeLocations(game_directory)` returns all candidates as `(type: Local | Global | SpecificVersion, version, directory)`.
- **One choice**: `ChooseReshadeVersion(locations, setting)` returns one directory (and optional fallback info).
- **One load path**: Loader uses only that directory; no separate “try local first” step, so override (latest / global / specific version) is always respected.
- **Versions**: Normalize to X.Y.Z; compare numerically for “latest” and for specific-version matching.

This design fixes the bug where a local 6.7.2 was loaded despite override 6.7.3, and keeps all “where to load ReShade from” logic in one place.
