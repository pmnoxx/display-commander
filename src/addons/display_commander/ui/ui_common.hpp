#pragma once

#include "../addon.hpp"
#include <vector>
#include "new_ui/new_ui_tabs.hpp"

// External declarations for settings


extern float s_reflex_debug_output;



// Forward declarations for settings sections
namespace renodx::ui {
    void AddWindowPositionSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddHdrColorspaceSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddNvapiSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddDeviceInfoSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddWindowInfoSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddDxgiDeviceInfoDetailedSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddDxgiCompositionInfoSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddIndependentFlipFailuresSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddReflexSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
    void AddDxgiDeviceInfoSettings(std::vector<renodx::utils::settings2::Setting*>& settings);
}
