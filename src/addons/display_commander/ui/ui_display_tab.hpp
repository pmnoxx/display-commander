#pragma once

#include <imgui.h>
#include <reshade.hpp>
#include <string>
#include <vector>


namespace ui {

// Initialize the display cache for the UI
void InitializeDisplayCache();

// Get monitor labels using the display cache
std::vector<std::string> GetMonitorLabelsFromCache();

// Find monitor index by device ID
int FindMonitorIndexByDeviceId(const std::string &device_id);

// Get the correct monitor index for target monitor selection
int GetTargetMonitorIndex();

} // namespace ui
