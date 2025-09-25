#pragma once

#include <string>
#include <vector>

namespace ui::monitor_settings {
// Handle auto-detection of current display settings
void HandleAutoDetection();

// Handle monitor selection UI
void HandleMonitorSelection(const std::vector<std::string> &monitor_labels);

// Handle resolution selection UI
void HandleResolutionSelection(int selected_monitor_index);

// Handle refresh rate selection UI
void HandleRefreshRateSelection(int selected_monitor_index, int selected_resolution_index);

// Handle apply display settings at start checkbox
void HandleApplyDisplaySettingsAtStartCheckbox();

// Handle auto-restore resolution checkbox
void HandleAutoRestoreResolutionCheckbox();

// Handle the "Apply with DXGI API" button
void HandleDXGIAPIApplyButton();

// Handle the pending confirmation UI and countdown/revert logic
void HandlePendingConfirmationUI();

} // namespace ui::monitor_settings
