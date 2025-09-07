#pragma once

#include <atomic>
#include "renodx/settings.hpp"

// External declarations for settings






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

    void AddDxgiDeviceInfoSettings(std::vector<utils::settings2::Setting*>& settings);
}
