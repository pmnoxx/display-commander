#pragma once

// Read-only NVIDIA driver profile (DRS) queries for DLSS SR/RR render preset overrides.
// No full Profile Inspector UI — logic lives here for Debug tab, DLSS Control panel, and overlay.

#include <cstdint>
#include <memory>
#include <string>

namespace display_commander::features::nvidia_profile_inspector {

struct DriverDlssRenderPresetSnapshot {
    bool query_succeeded = false;
    std::string error_message;
    std::string current_exe_path_utf8;
    bool has_profile = false;
    std::string profile_name;
    bool sr_defined_in_profile = false;
    bool rr_defined_in_profile = false;
    std::uint32_t sr_value_u32 = 0;
    std::uint32_t rr_value_u32 = 0;
    std::string sr_display;
    std::string rr_display;
    bool sr_is_non_default_override = false;
    bool rr_is_non_default_override = false;
};

struct DriverDlssProfileAutoCreateStatus {
    bool attempted = false;
    bool succeeded = false;
    bool created_profile = false;
    std::string message;
};

/** Single-line summary for overlay / DLSS Control: driver (DRS) override vs Display Commander combo. */
struct MergedDlssRenderPresetText {
    std::string primary;
    std::string tooltip;
    bool warn_color = false;
};

/**
 * Primary text: driver non-default DRS value when present, else DC combo (or "Preset override off").
 * Tooltip lists both DRS and DC. warn_color when DRS has a non-default override.
 * `drv` may be null (treat as DRS unavailable). Inline so DC_LITE builds can use this without linking
 * `nvidia_profile_inspector.cpp`.
 */
inline MergedDlssRenderPresetText MergeDriverAndDcRenderPreset(bool is_rr,
                                                               const DriverDlssRenderPresetSnapshot* drv,
                                                               bool dc_override_enabled,
                                                               const std::string& dc_combo_value) {
    MergedDlssRenderPresetText out{};
    const std::string dc_primary = dc_override_enabled ? dc_combo_value : std::string("Preset override off");

    if (drv == nullptr || !drv->query_succeeded) {
        out.primary = dc_primary;
        out.warn_color = false;
        out.tooltip = "DRS: unavailable";
        if (drv != nullptr && !drv->error_message.empty()) {
            out.tooltip += " (";
            out.tooltip += drv->error_message;
            out.tooltip += ")";
        }
        out.tooltip += "\nDC: ";
        out.tooltip += dc_primary;
        return out;
    }

    if (!drv->has_profile) {
        out.primary = dc_primary;
        out.warn_color = false;
        out.tooltip = "DRS: no NVIDIA profile for this exe\nDC: ";
        out.tooltip += dc_primary;
        return out;
    }

    const bool driver_override =
        is_rr ? drv->rr_is_non_default_override : drv->sr_is_non_default_override;
    const std::string& drs_display = is_rr ? drv->rr_display : drv->sr_display;

    if (driver_override) {
        out.primary = drs_display;
        out.warn_color = true;
        out.tooltip = "DRS: ";
        out.tooltip += drs_display;
        out.tooltip += " (driver override)\nDC: ";
        out.tooltip += dc_primary;
    } else {
        out.primary = dc_primary;
        out.warn_color = false;
        out.tooltip = "DRS: ";
        out.tooltip += drs_display;
        out.tooltip += " (inherit / default)\nDC: ";
        out.tooltip += dc_primary;
    }
    return out;
}

// Cached snapshot (sticky until InvalidateDriverDlssRenderPresetCache or force_refresh). Safe from UI threads;
// uses SRWLOCK for refresh + atomic shared_ptr.
std::shared_ptr<const DriverDlssRenderPresetSnapshot> GetDriverDlssRenderPresetSnapshot(bool force_refresh = false);

void InvalidateDriverDlssRenderPresetCache();
DriverDlssProfileAutoCreateStatus GetDriverDlssProfileAutoCreateStatus();

}  // namespace display_commander::features::nvidia_profile_inspector
