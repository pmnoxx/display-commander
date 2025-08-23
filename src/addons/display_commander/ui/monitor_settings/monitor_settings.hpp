#pragma once

#include <vector>
#include <string>

namespace renodx::ui::monitor_settings {

// Handle display cache refresh logic (every 60 frames)
void HandleDisplayCacheRefresh();

// Handle auto-detection of current display settings
void HandleAutoDetection();

// Handle monitor selection UI
void HandleMonitorSelection(const std::vector<std::string>& monitor_labels);

// Handle resolution selection UI
void HandleResolutionSelection(int selected_monitor_index);

// Handle refresh rate selection UI
void HandleRefreshRateSelection(int selected_monitor_index, int selected_resolution_index);

// Handle auto-restore resolution checkbox
void HandleAutoRestoreResolutionCheckbox();

// Handle auto-apply resolution and refresh rate checkboxes
void HandleAutoApplyCheckboxes();

// Handle the "Apply with DXGI API" button
void HandleDXGIAPIApplyButton();

// Handle the pending confirmation UI and countdown/revert logic
void HandlePendingConfirmationUI();

} // namespace renodx::ui::monitor_settings
