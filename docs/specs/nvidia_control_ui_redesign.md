# NVIDIA Control UI Redesign (Main Tab)

## 1. Goal & Scope

- **Goal**: Make the Main tab **NVIDIA Control** section easier to scan and use by grouping related settings, using checkboxes for enable toggles, and showing sub-options only when the parent is enabled. Sliders should replace combos where a numeric range is the natural control.
- **Scope**: Main tab only (`main_new_tab.cpp`). The NVIDIA Profile tab (full list / advanced) remains the source of truth for all driver settings; this spec only affects how the curated “NVIDIA Control” subset is presented on the Main tab.
- **Reference pattern**: Use the same subsection style as **DLSS indicator (Registry)** — i.e. `TreeNodeEx` with optional `ImGuiTreeNodeFlags_DefaultOpen`, indented content, and a short description/tooltip on the tree node.

---

## 2. Current State (Analysis)

### 2.1 Data source

- Settings shown in NVIDIA Control come from `GetRtxHdrSettingIds()` plus filtering from `important_settings` and `advanced_settings` in the cached profile search result.
- **Current order** (in code): Smooth Motion (Allowed APIs, Enable) → RTX HDR (Enable, Debanding, Allow, Contrast, Middle Grey, Peak Brightness, Saturation) → Max Pre-Rendered Frames → Ultra Low Latency (CPL State, Enabled).
- All settings are rendered in a **single flat table** (Setting | Value) with combo + “Default” button (and special handling for Smooth Motion - Allowed APIs “Allow - All” button).

### 2.2 Settings inventory (Main tab)

| Group / concept        | Setting IDs / names                                                                 | Value type / notes |
|------------------------|--------------------------------------------------------------------------------------|--------------------|
| **Smooth Motion**      | Smooth Motion - Allowed APIs [40 series], Smooth Motion - Enable                     | Bitfield (APIs), On/Off |
| **RTX HDR**            | RTX HDR - Enable                                                                     | On/Off             |
|                        | RTX HDR - Debanding, Allow                                                           | Enum (Debanding), Allow/Disallow (Allow may be requires_admin, filtered from Main tab) |
|                        | RTX HDR - Contrast, Middle Grey, Peak Brightness, Saturation                         | Numeric with presets + “Custom”; ranges 0–200, 10–100, 400–2000, 0–200 |
| **Latency**            | Max pre-rendered frames                                                              | 0 (app) or 1–8     |
|                        | Ultra Low Latency - CPL State, Ultra Low Latency - Enabled                           | Off/On/Ultra; Off/On |

- Note: `requires_admin` settings are currently removed from the Main tab list **except** Smooth Motion - Allowed APIs. Debanding and Allow are therefore not shown on Main tab today; the spec can keep that or document “show when RTX HDR enabled” for future inclusion.

### 2.3 Pain points

- **Flat list**: Many unrelated settings in one table (Smooth Motion, RTX HDR tuning, latency) — hard to find the “enable” switch and its children.
- **No hierarchy**: RTX HDR - Enable is just one row; the six other RTX HDR settings are not visually grouped, so it’s unclear they only matter when RTX HDR is on.
- **Combo-heavy**: Contrast, Middle Grey, Peak Brightness, Saturation have discrete presets plus “Custom”; for “Custom” and for quick tweaks, a slider would be more direct.
- **Low latency scattered**: Max pre-rendered frames and Ultra Low Latency (CPL State, Enabled) sit in the same table as HDR/Smooth Motion with no subsection.

---

## 3. Proposed UI Structure

Keep the top-level **CollapsingHeader("NVIDIA Control")** and the existing block (Refresh, Create profile, error state, restart warning). **Replace the single table** with **subsections** as below. Use `TreeNodeEx` for each subsection (same style as “DLSS indicator (Registry)”).

Suggested order:

1. **Smooth Motion** (subsection)
2. **RTX HDR** (subsection)
3. **Low latency** (subsection)

No separate subsection for “Max pre-rendered frames” alone; it lives inside **Low latency**.

---

## 4. Subsection Specifications

### 4.1 Smooth Motion

- **Node**: `TreeNodeEx("Smooth Motion", ...)` (default open or closed per preference; suggest **DefaultOpen** so it matches visibility of “one main feature”).
- **Content when expanded**:
  - **Smooth Motion - Enable**: Prefer a **checkbox** (On/Off) instead of a combo when the setting has exactly two values. If the profile exposes “Use global (Default)”, keep a small link or combo entry for “Use global (Default)” next to or below the checkbox so users can revert to global.
  - **Smooth Motion - Allowed APIs**: Keep current behavior: show current value text + **“Allow - All [DX11/12, VK]”** button. Optionally show only when “Smooth Motion - Enable” is On (if we want strict hierarchy); otherwise show always so users can set APIs before enabling.
- **Sliders**: Smooth Motion has no numeric range; no sliders here.

### 4.2 RTX HDR

- **Node**: `TreeNodeEx("RTX HDR", ...)`.
- **Content when expanded**:
  - **RTX HDR - Enable**: **Checkbox** (On/Off), with optional “Use global (Default)” as today.
  - **Conditional block**: All other RTX HDR settings (Debanding, Allow, Contrast, Middle Grey, Peak Brightness, Saturation) are shown **only when RTX HDR - Enable is On** (profile value or effective value if “Use global”). If the profile has “Use global” and global is On, show the block; if global is Off, hide it. This keeps the UI focused and avoids clutter when RTX HDR is off.
  - **Sliders** (when the subsection is visible):
    - **RTX HDR - Contrast**: Numeric range 0–200 (driver value 0x00–0xC8; “Custom” = 0xC9 can map to a slider value). Prefer **slider** (e.g. 0–200) with optional preset labels or a small “Presets” dropdown beside it. If we keep combo, at least use slider when current value is “Custom”.
    - **RTX HDR - Middle Grey**: Range 10–100; **slider**.
    - **RTX HDR - Peak Brightness**: Range 400–2000 (nits); **slider** (e.g. 400–2000 step 100).
    - **RTX HDR - Saturation**: Range 0–200; **slider**.
  - **Non-slider**: Debanding, Allow remain combo/dropdown (enum or On/Off). If we later expose “Allow” on Main tab when RTX HDR is enabled, keep as combo.
- **Tooltip on node**: e.g. “Enable and tune RTX HDR for this profile. Sub-options only apply when RTX HDR is enabled.”

### 4.3 Low latency (subsection)

- **Node**: `TreeNodeEx("Low latency", ...)` (or “Latency” — same idea). Same visual pattern as “DLSS indicator (Registry)”: small subsection, indented content.
- **Content when expanded**:
  - **Ultra Low Latency - CPL State**: Combo (Off / On / Ultra) + “Use global (Default)” + “Default” button as today.
  - **Ultra Low Latency - Enabled**: Combo (Off / On) + same.
  - **Max pre-rendered frames**: **Slider** (0 = application setting, 1–8) or combo; slider is friendlier for 1–8. If 0 is “Use the 3D application setting”, show as first option (e.g. “App (0)” or a checkbox “Use application setting” that clears the profile value) then slider 1–8.
- **Tooltip on node**: e.g. “Ultra Low Latency and max pre-rendered frames for this profile.”

---

## 5. Implementation Notes

### 5.1 Backend

- **No change to** `GetRtxHdrSettingIds()` or to which setting IDs are in the Main tab list; only the **presentation** and **grouping** change.
- Resolve “RTX HDR - Enable” current value (from profile or global) to drive the conditional visibility of the RTX HDR child block. Reuse existing `ImportantProfileSetting` / `GetCachedProfileSearchResult()` and `set_in_profile` / `value_id`; if “Use global”, may need to know global default or current effective value (driver may expose it; otherwise assume Off when not in profile).

### 5.2 Sliders and driver values

- RTX HDR Contrast/Middle Grey/Peak Brightness/Saturation: driver uses fixed hex values for presets and sometimes a “Custom” sentinel (e.g. 0xC9). For slider:
  - Map slider range (e.g. 0–200) to the driver value (0x00–0xC8) and vice versa when the value is in range.
  - If current profile value is a preset (e.g. 100 | 0), slider shows 100; moving slider writes the corresponding hex value (may be “Custom” if driver requires a distinct value for custom).
  - Check `nvidia_profile_search.cpp` / `GetSettingAvailableValues` and option_values for each setting to see exact mapping; some have “Custom (0–200)” with value 0xC9 — in that case writing 0–200 may require a separate NVAPI path or the driver may accept raw 0x00–0xC8. Document in code comments.
- Max pre-rendered frames: values 0 (app), 1–8; slider can be “0 = App, 1–8” or two controls (checkbox “App controlled” + slider 1–8). Existing option_values already list 0–8.

### 5.3 “Use global (Default)” and Default button

- Keep both in each subsection where they exist today (combo first option + “Default” button). For checkbox settings (RTX HDR - Enable, Smooth Motion - Enable), either:
  - Keep a small combo or link “Use global (Default)” next to the checkbox, or
  - Keep one row with checkbox + “Default” that sets driver default; and a separate “Use global (Default)” that calls `DeleteProfileSettingForCurrentExe`.

### 5.4 Admin-only and filtering

- Current rule: Main tab hides `requires_admin` except Smooth Motion - Allowed APIs. If we later show RTX HDR - Allow or Debanding when RTX HDR is enabled, keep the same rule (hide if requires_admin) unless we decide to show them in warning color with tooltip.

### 5.5 Restart warning

- Keep the existing “Restart the game for profile changes to take effect.” below the whole NVIDIA Control block when `s_nvidiaProfileChangeRestartNeeded` is true.

---

## 6. Summary of Changes

| Area           | Current                         | Proposed |
|----------------|----------------------------------|----------|
| Layout         | Single table, all settings      | Three subsections: Smooth Motion, RTX HDR, Low latency |
| RTX HDR        | All 8 rows always visible        | Enable = checkbox; other 7 rows only when Enable is On |
| RTX HDR values | All combos                       | Contrast, Middle Grey, Peak Brightness, Saturation → sliders (with preset/combo fallback if needed) |
| Smooth Motion  | Two rows in big table            | Subsection; Enable → checkbox; Allowed APIs unchanged |
| Low latency    | Three rows mixed in table        | Subsection with ULL CPL State, ULL Enabled, Max pre-rendered frames (slider 0–8) |
| Subsection UI  | N/A                              | `TreeNodeEx` per group, same style as “DLSS indicator (Registry)” |

---

## 7. Optional / Future

- **Default open/closed**: Decide default state of each TreeNodeEx (e.g. all closed to reduce scroll, or Smooth Motion + RTX HDR open by default).
- **Persist subsection open state**: If ImGui doesn’t persist tree state, consider storing open/closed in config so returning users see their preferred layout.
- **NVIDIA Profile tab**: No change in this spec; the Profile tab can later add similar grouping for consistency if desired.
