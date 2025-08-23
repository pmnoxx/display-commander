#pragma once

#include <vector>
#include "../renodx/settings.hpp"
#include "../display_cache.hpp"

namespace renodx::ui {
    void AddDisplayTabSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    
    // Initialize the display cache for the UI
    void InitializeDisplayCache();
    
    // Get monitor labels using the display cache
    std::vector<std::string> GetMonitorLabelsFromCache();
    
    // Get current display info using the display cache
    std::string GetCurrentDisplayInfoFromCache();
    
    // Handle monitor settings UI (extracted from on_draw lambda)
    bool HandleMonitorSettingsUI();
}
