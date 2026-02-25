#pragma once

#include <string>
#include <vector>
#include "../new_ui/settings_wrapper.hpp"

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}

namespace ui::monitor_settings {

// Global settings variables
extern ui::new_ui::BoolSetting g_setting_auto_apply_resolution;
extern ui::new_ui::BoolSetting g_setting_auto_apply_refresh;
extern ui::new_ui::BoolSetting g_setting_apply_display_settings_at_start;

// Handle auto-detection of current display settings (no UI)
void HandleAutoDetection();

// Draw full monitor settings UI (resolution/refresh/apply). Uses wrapper for ReShade or standalone.
void DrawMonitorSettings(display_commander::ui::IImGuiWrapper& imgui);

// Handle monitor selection UI
void HandleMonitorSelection(display_commander::ui::IImGuiWrapper& imgui,
                            const std::vector<std::string>& monitor_labels);

// Handle resolution selection UI
void HandleResolutionSelection(display_commander::ui::IImGuiWrapper& imgui, int selected_monitor_index);

// Handle refresh rate selection UI
void HandleRefreshRateSelection(display_commander::ui::IImGuiWrapper& imgui, int selected_monitor_index,
                                int selected_resolution_index);

// Handle apply display settings at start checkbox
void HandleApplyDisplaySettingsAtStartCheckbox(display_commander::ui::IImGuiWrapper& imgui);

// Handle auto-restore resolution checkbox
void HandleAutoRestoreResolutionCheckbox(display_commander::ui::IImGuiWrapper& imgui);

// Handle the "Apply with DXGI API" button
void HandleDXGIAPIApplyButton(display_commander::ui::IImGuiWrapper& imgui);

// Handle the pending confirmation UI and countdown/revert logic
void HandlePendingConfirmationUI(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::monitor_settings
