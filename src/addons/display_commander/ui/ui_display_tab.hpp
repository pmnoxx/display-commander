#pragma once

#include <imgui.h>
#include <reshade.hpp>
#include <string>
#include <vector>


namespace ui {

// Find monitor index by device ID
int FindMonitorIndexByDeviceId(const std::string &device_id);

} // namespace ui
