# NVIDIA Profile Search – Simple Design (No Enumeration)

**Goal:** Simplify the DRS (NVIDIA driver profile) code, remove bugs, and use a single lookup path. **Do not enumerate profiles.** Support only five operations: find profile, delete profile, create profile, get profile details, set setting.

---

## 1. Design Principle: No Enumeration

- **Single way to resolve “profile for current exe”:** `NvAPI_DRS_FindApplicationByName(session, full_exe_path, &hProfile, &app)`. One profile or none.
- **No** `NvAPI_DRS_EnumProfiles`, **no** `NvAPI_DRS_EnumApplications` for “finding” a profile.
- **No** `NvAPI_DRS_FindProfileByName` (find by path only).

Result: one code path, fewer bugs, simpler behavior.

---

## 2. The Five Operations

| # | Operation | Description |
|---|-----------|-------------|
| 1 | **Find profile** | For current exe: get full path → `NvAPI_DRS_FindApplicationByName` → returns profile handle + app, or “not found”. Single API call. |
| 2 | **Delete profile** | For current exe: find by path → get profile info → if name is “Display Commander - …”, delete that profile and save. |
| 3 | **Create profile** | For current exe: find by path; if found, return success; if not found, create profile “Display Commander - &lt;exe&gt;”, add application, save. |
| 4 | **Get profile details** | For current exe: find by path; if found, return that profile’s info (name, app entry) + important_settings + advanced_settings + all_settings (via GetSetting / EnumSettings on that one profile). No list of “matching profiles”. |
| 5 | **Set setting** | For current exe: find by path → set DWORD setting → save. |

---

## 3. API Shape (Simplified)

### 3.1 Find (internal / shared)

- **FindApplicationByPathForCurrentExe(hSession, phProfile, pApp) → bool**  
  - Only place that calls `NvAPI_DRS_FindApplicationByName` (current process full path).  
  - Caller owns session. Returns true if profile found.

### 3.2 Public API (no enumeration)

- **GetProfileDetailsForCurrentExe()** (replaces SearchAllProfilesForCurrentExe + GetCachedProfileSearchResult)  
  - Create session, load settings, **find by path**.  
  - If not found: success=true, no profile (empty profile name, no settings).  
  - If found: fill **one** profile name, **one** app entry (for UI), important_settings, advanced_settings, all_settings from that profile.  
  - Cache result for UI (invalidate on create/delete/set).

- **CreateProfileForCurrentExe()**  
  - Find by path; if found return success; else create profile + app, save. Invalidate cache.

- **DeleteDisplayCommanderProfileForCurrentExe()**  
  - Find by path; get profile info; if name starts with "Display Commander - ", delete profile, save. Invalidate cache.

- **SetProfileSetting(settingId, value)**  
  - Find by path; if not found return error; else set setting, save. Invalidate cache.

- **SetOrDeleteProfileSettingForExe(exeName, …)**  
  - **Only support current exe:** when exeName matches current process (e.g. same base name and we use current path for lookup), use find by path.  
  - **Drop support for “other exe”** (no enumeration): return error like “Only current process exe is supported” if exeName is not the current process.

Optional: **GetSettingAvailableValues(settingId)** and **ClearDriverDlssPresetOverride** unchanged (they use set/get on the profile found by path).

---

## 4. Data Model (Get Profile Details Result)

**Current:** `NvidiaProfileSearchResult` with vectors: matching_profiles, matching_profile_names, important_settings, advanced_settings, all_settings.

**Simplified (backward compatible for UI):**

- Keep **one** struct (e.g. still `NvidiaProfileSearchResult` or rename to `ProfileDetailsForCurrentExe`).
- **matching_profile_names:** size 0 or 1 (empty if no profile, one element if found).
- **matching_profiles:** size 0 or 1 (one `MatchedProfileEntry` if found).
- **important_settings / advanced_settings / all_settings:** from that single profile (or empty if no profile).
- **success / error / current_exe_path / current_exe_name:** unchanged.

UI already checks `matching_profile_names.empty()` and iterates; changing to 0-or-1 keeps it working with minimal changes.

---

## 5. What Gets Removed

| Item | Action |
|------|--------|
| **NvAPI_DRS_EnumProfiles** | Remove all uses. |
| **NvAPI_DRS_EnumApplications** | Remove all uses (no “find profile by exe” via enumeration). |
| **NvAPI_DRS_FindProfileByName** | Remove all uses. |
| **FindFirstMatchingProfile** | Remove. |
| **SearchAllProfilesForCurrentExe** | Replace with **GetProfileDetailsForCurrentExe** (find by path + get profile info + read settings for that one profile). |
| **Enumeration helper for “other exe”** | Do not add. SetOrDeleteProfileSettingForExe supports only current exe. |
| **AppMatchesExe / multi-profile matching** | Remove (no enumeration). |

---

## 6. Implementation Phases

### Phase 1 – Plan
- [x] Write this plan in `doc/tasks`.

### Phase 2 – Find only (single entry point)
- [x] **FindApplicationByPathForCurrentExe(hSession, phProfile, pApp) → bool.**  
  - Implement with single call to `NvAPI_DRS_FindApplicationByName` (current exe path).  
  - Declare in header (e.g. `nvidia_profile_search.hpp`) so fullscreen_prevention can use it; header may need `#include <nvapi.h>` or document “include nvapi before this header”.
- [x] **nvapi_fullscreen_prevention.cpp:** use FindApplicationByPathForCurrentExe instead of calling FindApplicationByName directly.

### Phase 3 – Get profile details (no enumeration)
- [x] **GetProfileDetailsForCurrentExe()** (replaces SearchAllProfilesForCurrentExe):  
  - Create session, load settings.  
  - Find by path (FindApplicationByPathForCurrentExe).  
  - If not found: return result with success=true, empty profile name/settings.  
  - If found: NvAPI_DRS_GetProfileInfo, build one MatchedProfileEntry, ReadImportantSettings, ReadAdvancedSettings, ReadAllSettings for that profile.  
  - Fill NvidiaProfileSearchResult with matching_profile_names.size() 0 or 1, matching_profiles.size() 0 or 1.
- [x] **GetCachedProfileSearchResult()** → call GetProfileDetailsForCurrentExe() (with cache + InvalidateProfileSearchCache). Keep same name for UI or rename to “get cached profile details” as desired.
- [x] **GetDlssDriverPresetStatus()** – keep using cached result (one profile).
- [x] **HasDisplayCommanderProfile(r)** – check the single profile name (r.matching_profile_names has 0 or 1 element).

### Phase 4 – Create, delete, set (all via find-by-path)
- [x] **SetProfileSetting:** create session, load, FindApplicationByPathForCurrentExe; if not found return error; else set setting, save, invalidate cache.
- [x] **CreateProfileForCurrentExe:** create session, load, FindApplicationByPathForCurrentExe; if found return success; else create profile + app, save, invalidate cache.
- [x] **DeleteDisplayCommanderProfileForCurrentExe:** create session, load, FindApplicationByPathForCurrentExe; if not found return “no profile”; get profile info; if name starts with "Display Commander - ", delete profile, save, invalidate cache; else return “not a Display Commander profile”.
- [x] **SetOrDeleteProfileSettingForExe:** only support when exeName corresponds to current process (use current exe path for lookup); use FindApplicationByPathForCurrentExe; if not current exe, return error “Only current process exe is supported”.

### Phase 5 – Remove old code
- [x] Remove FindFirstMatchingProfile.
- [x] Remove all NvAPI_DRS_FindProfileByName usage.
- [x] Remove all NvAPI_DRS_EnumProfiles / NvAPI_DRS_EnumApplications usage (except possibly EnumSettings for “all settings” of the one profile).
- [x] Remove AppMatchesExe and any enumeration-only helpers.
- [x] Clean up GetProfilePathForCurrentExe if only used by FindApplicationByPathForCurrentExe (keep as internal helper).

### Phase 6 – Verify
- [x] Build succeeds.
- [ ] Run, test: profile tab (no profile / one profile, create, delete, set settings), DLSS preset status, fullscreen prevention, ClearDriverDlssPresetOverride.
- [x] Lint and tidy (unused include removed; C-style cast fixed).

---

## 7. Success Criteria

- **No profile enumeration** for resolving “profile for current exe”.
- **One call site** for `NvAPI_DRS_FindApplicationByName`: inside FindApplicationByPathForCurrentExe (current exe path only).
- **Zero** uses of `NvAPI_DRS_FindProfileByName`, `NvAPI_DRS_EnumProfiles`, `NvAPI_DRS_EnumApplications` (for finding profiles).
- Only five logical operations: find profile, delete profile, create profile, get profile details, set setting.
- Simpler code, fewer bugs, easier to maintain.

---

## 8. Files Touched

- `src/addons/display_commander/nvapi/nvidia_profile_search.cpp` – implement five operations, remove enumeration.
- `src/addons/display_commander/nvapi/nvidia_profile_search.hpp` – optional: declare FindApplicationByPathForCurrentExe; simplify comments (get profile details = one profile).
- `src/addons/display_commander/nvapi/nvapi_fullscreen_prevention.cpp` – use FindApplicationByPathForCurrentExe.
- `src/addons/display_commander/ui/nvidia_profile_tab_shared.cpp` – minimal: “Matching profile(s)” becomes 0 or 1; rest unchanged if result shape is compatible.
