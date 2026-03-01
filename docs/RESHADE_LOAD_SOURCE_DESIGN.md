# ReShade Load Source Feature – Design Plan

## 1. Overview

Add an option on the **ReShade tab** to choose where ReShade is loaded from when Display Commander runs as a proxy (e.g. dxgi.dll) and needs to load ReShade itself:

1. **Local folder (default)** – Current behavior: load from `%localappdata%\Programs\Display_Commander\Reshade\` (flat: `Reshade64.dll` / `Reshade32.dll`).
2. **Shared path** – User-provided directory that contains `Reshade64.dll` and `Reshade32.dll` (e.g. network or common install).
3. **Specific version** – Load from `%localappdata%\Programs\Display_Commander\Reshade\X.Y.Z\` (e.g. `6.6.2`, `6.7.3`). If that folder is missing, the user can trigger a **download** of the ReShade Addon installer, **X.509 signature verification**, and **extraction** of the DLLs into that folder; **status** is shown in the UI.

This document iterates until all implementation details are covered.

---

## 2. Current Behavior (Summary)

- **ReShade tab**: `addons_tab.cpp` draws the ReShade tab content (when `show_reshade_tab` is on); it uses `GetReshadeDirectory()` which returns the **flat** path `...\Display_Commander\Reshade` (no version subfolder). `Reshade64DllExists()` / `Reshade32DllExists()` and version helpers use that same flat path.
- **ProcessAttach** (`main_entry.cpp`): When ReShade is not already in the process, `ProcessAttach_TryLoadReShadeFromCwd()` tries CWD first; then `ProcessAttach_TryLoadReShadeWhenNotLoaded()` uses a **hardcoded** path: `%localappdata%\Programs\Display_Commander\Reshade\` + `Reshade64.dll` or `Reshade32.dll`. No setting is read at that point.
- **Standalone UI** (`cli_standalone_ui.cpp`): Already has ReShade download + extract: `DownloadBinaryFromUrl` → temp → `tar -xf ReShade_Setup_*_Addon.exe ReShade64.dll ReShade32.dll` (Windows 10+ `tar`), then copy to a central dir. **No Authenticode verification** today. Versions from a hardcoded list. The new ReShade-tab download flow can use the same approach (tar.exe) for now.

---

## 3. Requirements (Refined)

| # | Requirement |
|---|-------------|
| R1 | User can choose: **Local folder** (default), **Shared path**, or **Specific version**. |
| R2 | **Local**: Use existing flat folder `...\Display_Commander\Reshade\` (current behavior). |
| R3 | **Shared path**: Use a user-configurable directory; must contain `Reshade64.dll` and `Reshade32.dll` (or at least the one for current process bitness). |
| R4 | **Specific version**: Use `...\Display_Commander\Reshade\X.Y.Z\` (e.g. `6.6.2`, `6.7.3`). Supported versions at least `["6.6.2", "6.7.3"]` (extensible). |
| R5 | If **Specific version** is selected but the version folder does **not** exist (or DLLs missing), user can trigger: **Download** from `https://reshade.me/downloads/ReShade_Setup_X.Y.Z_Addon.exe` → **Verify** X.509 Digital Signature (thumbprint `589690208A5E52FB96980C4A6698F50ACD47C49F`) → **Extract** `Reshade32.dll` and `Reshade64.dll` into `...\Reshade\X.Y.Z\`. |
| R6 | **Status** in ReShade tab: show whether the chosen source is available (e.g. “Ready”, “Not found”, “Downloading…”, “Error: …”). |
| R7 | Setting must be **effective at ProcessAttach** (before ReShade is loaded). Therefore it cannot rely on ReShade config; it must be stored in **Display Commander config** (file-based, readable at startup). |

---

## 4. Persistence and Where to Read the Setting

- **Storage**: Display Commander config (TOML), e.g. section `DisplayCommander.ReShade` (or `ReShade`):
  - `ReshadeLoadSource` (int): `0` = Local, `1` = SharedPath, `2` = SpecificVersion.
  - `ReshadeSharedPath` (string): path when source = SharedPath (can be empty).
  - `ReshadeSelectedVersion` (string): e.g. `"6.7.3"` when source = SpecificVersion.
- **Why not ReShade config only**: At `ProcessAttach_TryLoadReShadeWhenNotLoaded`, ReShade is not loaded yet, so ReShade’s config is not available. DC config is already initialized earlier in ProcessAttach, so we read these three keys from DC config there.
- **ReShade tab UI**: Reads/writes these via `display_commander::config::DisplayCommanderConfigManager::GetInstance()` (GetConfigValue / SetConfigValue) and `SaveConfig()`. No need to duplicate in ReShade tab settings wrapper unless we want the same keys in ReShade config for consistency when ReShade is running; for simplicity, **only DC config** is used for these three keys.

---

## 5. Path Resolution (Loader)

- **Single helper** used by both:
  - ProcessAttach loader in `main_entry.cpp`, and  
  - ReShade tab (for status and “Open folder”).
- **Signature**: e.g. `std::filesystem::path GetReshadeDirectoryForLoading();`  
  Returns the directory that should contain `Reshade64.dll` / `Reshade32.dll` for the current load source.
- **Logic**:
  - Read from DC config: `ReshadeLoadSource`, `ReshadeSharedPath`, `ReshadeSelectedVersion` (with defaults: 0, "", "6.7.3" or first in list).
  - If **Local (0)**: return `%localappdata%\Programs\Display_Commander\Reshade` (current flat path).
  - If **Shared (1)**: return `ReshadeSharedPath` (normalized, absolute if possible); if empty or invalid, fallback to Local path and optionally log.
  - If **Specific version (2)**: return `%localappdata%\Programs\Display_Commander\Reshade\<ReshadeSelectedVersion>` (e.g. `...\Reshade\6.7.3`).
- **ProcessAttach** (`ProcessAttach_TryLoadReShadeWhenNotLoaded`): Replace hardcoded `dc_reshade_dir` with `GetReshadeDirectoryForLoading()`. If the resolved path does not exist or the required DLL is missing, show the same style of message as today, but for “Specific version” mention that the user can use the ReShade tab to download that version.

---

## 6. ReShade Tab UI (Addons tab – ReShade content)

- **Placement**: In the same ReShade tab where “Suppress ReShade Clock” and “Global ReShade” live (`addons_tab.cpp`), add a new subsection (e.g. **“ReShade load source”** or **“Where to load ReShade”**).
- **Controls**:
  - **Combo or radio**: “Load ReShade from:”  
    - **Local folder (default)**  
    - **Shared path**  
    - **Specific version**
  - When **Shared path**:
    - Text input for directory path.
    - Optional “Browse” button (folder picker).
  - When **Specific version**:
    - Combo with at least `["6.6.2", "6.7.3"]` (hardcoded list; can be extended later).
    - **Status line**: one of:
      - “Ready” (both DLLs present in `...\Reshade\X.Y.Z\`)
      - “Not found – click Download to install”
      - “Downloading…”
      - “Ready (downloaded)”
      - “Error: &lt;message&gt;” (e.g. download failed, signature invalid, extract failed)
    - **Download** button: enabled when folder/DLLs are missing (or always; if already present, can show “Already installed” or still allow re-download). Click starts background download + verify + extract; UI shows “Downloading…” and then status.
- **Persistence**: On change, write `ReshadeLoadSource`, `ReshadeSharedPath`, `ReshadeSelectedVersion` to DC config and call `SaveConfig()`.
- **“Open Reshade Folder”**: When “Specific version” is selected, “Open Reshade Folder” should open `...\Reshade\X.Y.Z\`; otherwise keep current behavior (flat folder or shared path). So “Open Reshade Folder” uses `GetReshadeDirectoryForLoading()` (or the same resolved path as the loader).

---

## 7. Download, Verify, Extract (Specific version)

- **URL**: `https://reshade.me/downloads/ReShade_Setup_<version>_Addon.exe` (e.g. `6.7.3`).
- **Steps**:
  1. Download to temp file (e.g. `%TEMP%\dc_reshade_<version>.exe`).
  2. **Verify Authenticode**: Check that the PE file is signed and that the **X.509 certificate thumbprint** matches `589690208A5E52FB96980C4A6698F50ACD47C49F` (user-provided for 6.7.3; we use same for all supported versions unless we later add per-version thumbprints). Use WinVerifyTrust and then CertGetCertificateContext + thumbprint comparison (or equivalent). If verification fails, delete temp file, set status to error, do not extract.
  3. **Extract**: Use `tar.exe -xf "<exe>" ReShade64.dll ReShade32.dll` in a temp directory (same as standalone UI and `scripts/dc_service/download_dc32_winmm.bat`; Windows 10+ has tar). Alternatively a .bat could be used; for now tar.exe is acceptable.
  4. **Copy**: Copy `ReShade64.dll` and `Reshade32.dll` from temp to `%localappdata%\Programs\Display_Commander\Reshade\<version>\`. Create the version folder if missing; overwrite existing DLLs.
  5. Clean up temp files.
- **Threading**: Run in a **background thread** (e.g. worker thread or `CreateThread`), not on the UI thread. Use **atomics** (and optionally a simple status string) for “Downloading” / “Ready” / “Error” so the UI can poll and show status. **No std::mutex** (project rule: use SRWLOCK if a lock is needed).
- **Optional**: After extract, verify extracted DLLs with the existing **SHA256 database** (`GetReShadeExpectedSha256`) and show “signature: OK” / “MISMATCH” in status (same as standalone UI). If we do this, status can show “Ready (signature OK)” or “Warning: signature mismatch”.

**Extraction**: For now use tar.exe (see step 3 above). In-process extraction can be added later if desired.


---

## 8. Authenticode Verification (Detail)

- **API**: Use WinVerifyTrust to verify the PE signature; then use CertGetCertificateContext (or WTHelperProvDataFromStateData / CertFindCertificateInStore) to get the signer’s certificate and compute its SHA1 thumbprint (or the thumbprint type the user gave – typically SHA1). Compare with the expected thumbprint string (case-insensitive, spaces ignored).
- **Location**: New small utility (e.g. `utils/authenticode_verify.hpp` / `.cpp` or under `utils/version_check`) so both ReShade tab and any future use can call it. Signature: e.g. `bool VerifyAuthenticodeThumbprint(const std::wstring& pe_path, const std::string& expected_thumbprint_hex);`
- **Dependencies**: Windows APIs only (Wintrust.h, Crypt32.lib, etc.); no new third-party libs.

---

## 9. Status and Thread Safety

- **Status** for “Specific version” download:
  - Enum or string: `Idle` | `Downloading` | `Verifying` | `Extracting` | `Ready` | `Error`.
  - Error message string when status = Error.
- Store in **atomics** (e.g. `std::atomic<int> status_`, `std::atomic<std::string*> last_error_message_`) or a small struct guarded by SRWLOCK so the UI thread can read and the worker can write without blocking. Prefer atomics for status enum and a single pointer to a heap-allocated error string (worker sets it once when finishing with error).
- **Re-entrancy**: Only one download at a time (e.g. disable “Download” while status == Downloading).

---

## 10. Edge Cases and Clarifications

| Topic | Decision |
|-------|----------|
| **Empty shared path** | Treat as invalid; fall back to Local path for loading and show warning in UI (or keep last valid path). |
| **Invalid/network path** | LoadLibrary will fail; existing error handling in `ProcessAttach_TryLoadReShadeWhenNotLoaded` shows a message. UI can show “Path not found” if we check existence when drawing. |
| **Version list** | Start with `["6.6.2", "6.7.3"]`; later can add more (e.g. 6.7.2) or fetch from a list. |
| **Custom version string** | Phase 1: only combo with fixed list. Later: optional free-text version and validate format (e.g. `\d+\.\d+\.\d+`). |
| **Same thumbprint for all versions** | Use one thumbprint for all; if ReShade changes signer in future, we can add per-version thumbprint table. |
| **tar not available** | If tar.exe is missing (e.g. older Windows), show “Install failed: tar extract failed (need Windows 10+ tar).” |
| **Addons / Shaders / Textures paths** | Unchanged: still under the **base** Reshade dir (e.g. `...\Reshade\Addons`, `...\Reshade\Shaders`). When using “Specific version”, we have two concepts: (1) **Load path** = `...\Reshade\X.Y.Z\` for the DLLs; (2) **Base path** for Addons/Shaders/Textures can remain `...\Reshade\` (shared) so addons and shaders are not duplicated per version. So: Addons/Shaders/Textures stay at `...\Display_Commander\Reshade\Addons` etc.; only the **DLLs** live in `...\Reshade\X.Y.Z\` when using specific version. |
| **GetReshadeDirectory() in addons_tab** | Today it returns the flat folder. For “Open Reshade Folder” and status we need the **resolved** load directory. **Decision**: Add `GetReshadeDirectoryForLoading()` in a shared module (e.g. `utils/reshade_load_path.cpp`). In addons_tab, **replace** the current `GetReshadeDirectory()` implementation so it returns `GetReshadeDirectoryForLoading()` (or alias the name). Then `Reshade64DllExists()`, `Reshade32DllExists()`, `GetReshade64Version()`, `GetReshade32Version()` will naturally use the resolved path (they already build paths from “Reshade directory”), so “Global ReShade” and “Open Reshade Folder” will reflect the actual load source (Local / Shared / Specific version). No need to keep a separate “base” path for backward compatibility; the base for Addons/Shaders/Textures can remain `%localappdata%\Programs\Display_Commander\Reshade` when we need it (e.g. for Addons directory path), and only the **DLL directory** is the resolved one. |

---

## 11. Dependencies and ProcessAttach

- **Path resolver** (`GetReshadeDirectoryForLoading()`) must be callable from `main_entry.cpp` at ProcessAttach **without** pulling in ReShade (no `reshade.hpp`). It should depend only on: Display Commander config (DisplayCommanderConfigManager), Windows (SHGetFolderPathW), and filesystem. So the implementation lives in a separate source (e.g. `utils/reshade_load_path.cpp`) that does not include ReShade headers.
- **Config**: DisplayCommanderConfigManager is already initialized in `ProcessAttach_EarlyChecksAndInit` before `ProcessAttach_TryLoadReShadeWhenNotLoaded` runs, so reading the three keys from DC config there is safe.

---

## 12. File / Module Layout

| Item | Location / Notes |
|------|-------------------|
| DC config keys | Section `DisplayCommander.ReShade` (or `ReShade`); keys above. |
| Path resolver | New: e.g. `utils/reshade_load_path.hpp` + `.cpp` (or under `config/`) – `GetReshadeDirectoryForLoading()`, reads DC config. |
| Authenticode verify | New: `utils/authenticode_verify.hpp` + `.cpp` – `VerifyAuthenticodeThumbprint()`. |
| Download + verify + extract | New: e.g. `utils/reshade_version_download.hpp` + `.cpp` – one function or class that takes version string, runs in background, updates atomic status; uses `DownloadBinaryFromUrl`, `VerifyAuthenticodeThumbprint`, spawn `tar.exe -xf` (same as standalone UI), copy to `...\Reshade\<version>\`. Can be called from addons_tab when user clicks Download. |
| ReShade tab UI | `addons_tab.cpp`: new subsection “ReShade load source”; combo + path edit + version combo + status + Download button; read/write DC config. |
| ProcessAttach | `main_entry.cpp`: in `ProcessAttach_TryLoadReShadeWhenNotLoaded`, use `GetReshadeDirectoryForLoading()` instead of hardcoded path; message when DLLs missing for “Specific version” suggests using ReShade tab to download. |

---

## 13. Order of Implementation (Suggested)

1. **Config**: Define the three DC config keys and default values; ensure they are read/written in one place (path resolver + ReShade tab).
2. **Path resolver**: Implement `GetReshadeDirectoryForLoading()` using DC config; use it in `ProcessAttach_TryLoadReShadeWhenNotLoaded` and keep existing behavior for Local (flat path).
3. **ReShade tab UI**: Add “ReShade load source” subsection with combo (Local / Shared / Specific version), path edit + browse for Shared, version combo + status + Download for Specific version; persist to DC config.
4. **Authenticode**: Implement `VerifyAuthenticodeThumbprint()` and unit-test with a known-signed PE.
5. **Download pipeline**: Implement download → verify → extract → copy to `...\Reshade\X.Y.Z\` in a background thread; wire status to UI and Download button.
6. **Polish**: “Open Reshade Folder” uses resolved path; optional SHA256 check on extracted DLLs; error messages and tooltips.

---

## 14. Summary Table

| Aspect | Choice |
|--------|--------|
| **Persistence** | Display Commander config (TOML), section e.g. `DisplayCommander.ReShade` |
| **Load source values** | 0 = Local, 1 = Shared path, 2 = Specific version |
| **Shared path** | Single string; must point to folder with Reshade64/32.dll |
| **Specific version folder** | `%localappdata%\Programs\Display_Commander\Reshade\X.Y.Z\` |
| **Download URL** | `https://reshade.me/downloads/ReShade_Setup_X.Y.Z_Addon.exe` |
| **Signature** | X.509 thumbprint `589690208A5E52FB96980C4A6698F50ACD47C49F` |
| **Extract** | `tar.exe -xf` (same as standalone UI); copy Reshade64.dll + Reshade32.dll to version folder. |
| **Threading** | Background thread for download; atomics (or SRWLOCK) for status; no std::mutex |
| **Addons/Shaders/Textures** | Remain under base `...\Reshade\` (Addons, Shaders, Textures); only DLLs in version subfolder when using Specific version |

This design covers: where the option lives (ReShade tab), the three modes (local / shared / specific version), persistence (DC config for ProcessAttach), path resolution, download + verify + extract with status, and integration points. If you want, next step is to implement step-by-step following section 12.
