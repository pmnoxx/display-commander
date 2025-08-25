#pragma once

#include "../addon.hpp"
#include <vector>
#include "new_ui/new_ui_tabs.hpp"

// External declarations for settings


extern std::atomic<bool> s_reflex_debug_output;



// Forward declarations for settings sections
namespace ui {
    void AddWindowPositionSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddHdrColorspaceSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddNvapiSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddDeviceInfoSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddWindowInfoSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddDxgiDeviceInfoDetailedSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddDxgiCompositionInfoSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddIndependentFlipFailuresSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddReflexSettings(std::vector<utils::settings2::Setting*>& settings);
    void AddDxgiDeviceInfoSettings(std::vector<utils::settings2::Setting*>& settings);
}
