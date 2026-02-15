# DLSS Quality Profile APIs: NGX vs Streamline

Some games (e.g. **Wuthering Waves**) do **not** use `NVSDK_NGX_Parameter_PerfQualityValue` to set the DLSS quality profile. They use **NVIDIA Streamline** instead.

## Two paths

| Path | Used by | API that sets quality profile |
|------|--------|-------------------------------|
| **NGX** | Games that call NGX directly | `NVSDK_NGX_Parameter_SetI(params, NVSDK_NGX_Parameter_PerfQualityValue, value)` |
| **Streamline** | Games using the Streamline SDK (e.g. Wuthering Waves) | `slDLSSSetOptions(viewport, options)` with `options.mode` |

## Streamline API (sl.dlss)

- **Header**: `external/Streamline/include/sl_dlss.h`
- **Quality profile (mode)** is set via:
  - **`slDLSSSetOptions(viewport, options)`** — game passes `sl::DLSSOptions` where **`options.mode`** is the profile.
  - **`sl::DLSSMode`** enum: `eOff`, `eMaxPerformance`, `eBalanced`, `eMaxQuality`, `eUltraPerformance`, `eUltraQuality`, `eDLAA`.
- **Render presets** (A/B/C per mode) are set in the same struct:
  - `options.qualityPreset`, `options.balancedPreset`, `options.performancePreset`, `options.ultraPerformancePreset`, `options.ultraQualityPreset`, `options.dlaaPreset` (type `sl::DLSSPreset`: `eDefault`, `ePresetF`, … `ePresetK`, etc.).

The game **never** calls NGX `Parameter_SetI(PerfQualityValue)`. The Streamline **sl.dlss plugin** does that internally when it receives options via `slSetData` / when creating the feature (see `external/Streamline/source/plugins/sl.dlss/dlssEntry.cpp`):

- In `slSetData`: it sets NGX `DLSS.Hint.Render.Preset.*` from the preset fields.
- When creating the NGX feature or in `getOptimalSettings`: it sets `NVSDK_NGX_Parameter_PerfQualityValue` from `(uint32_t)consts->mode - 1` (SL mode eOff=0, then eMaxPerformance=1 … eDLAA=6; NGX uses 0–5 for the quality levels).

## Supporting quality override for Streamline games

To make “DLSS Quality Preset Override” work in Streamline games (e.g. Wuthering Waves):

1. **Hook `slDLSSSetOptions`** (same pattern as existing `slDLSSGSetOptions` / `slDLSSGetOptimalSettings` in `slGetFeatureFunction_Detour`).
2. In the detour, if the user has a non–“Game Default” **DLSS Quality Preset Override**:
   - Map the override string (e.g. `"Quality"`, `"Balanced"`) to **`sl::DLSSMode`**.
   - Copy the incoming `options` and set **`modified_options.mode`** to the overridden mode, then call the original with `modified_options`.

Mapping from our preset names to `sl::DLSSMode`:

- "Ultra Performance" → `sl::DLSSMode::eUltraPerformance`
- "Performance" / "Max Performance" → `sl::DLSSMode::eMaxPerformance`
- "Balanced" → `sl::DLSSMode::eBalanced`
- "Quality" / "Max Quality" → `sl::DLSSMode::eMaxQuality`
- "Ultra Quality" → `sl::DLSSMode::eUltraQuality`
- "DLAA" → `sl::DLSSMode::eDLAA`

Do **not** override to `eOff` unless the user explicitly chooses “Off” (if we add that).

## Optional: hook slDLSSGetOptimalSettings for override

Games may call `slDLSSGetOptimalSettings(options, settings)` with the desired mode in `options.mode` to get render dimensions. For a consistent override, we can also hook this and replace `options.mode` with the overridden mode so that resolution calculations and UI reflect the overridden profile. We already hook `slDLSSGetOptimalSettings` for observation; the same detour can apply the mode override before calling the original.

## References

- `external/Streamline/include/sl_dlss.h` — `DLSSOptions`, `DLSSMode`, `slDLSSSetOptions`, `slDLSSGetOptimalSettings`.
- `external/Streamline/source/plugins/sl.dlss/dlssEntry.cpp` — where `PerfQualityValue` and hint presets are set from `consts->mode` and preset fields.
- `src/addons/display_commander/hooks/streamline_hooks.cpp` — existing Streamline hooks; add `slDLSSSetOptions` (and optionally mode override in `slDLSSGetOptimalSettings`).
