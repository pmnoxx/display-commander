#pragma once

#include <vector>
#include "../renodx/settings.hpp"

namespace ui {
    void AddDisplayTabSettings(std::vector<utils::settings2::Setting*>& settings);

    // Initialize the display cache for the UI
    void InitializeDisplayCache();

    // Get monitor labels using the display cache
    std::vector<std::string> GetMonitorLabelsFromCache();

    // Find monitor index by device ID
    int FindMonitorIndexByDeviceId(const std::string& device_id);

    // Get the correct monitor index for target monitor selection
    int GetTargetMonitorIndex();

}
