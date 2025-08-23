#pragma once

#include <deps/imgui/imgui.h>
#include <functional>

namespace renodx::ui::new_ui {

// Draw the device info tab content
void DrawDeviceInfoTab();

// Draw basic device information
void DrawBasicDeviceInfo();

// Draw monitor information
void DrawMonitorInfo();

// Draw DXGI device information
void DrawDxgiDeviceInfo();

// Draw detailed DXGI device information
void DrawDxgiDeviceInfoDetailed();

// Draw HDR and colorspace controls
void DrawHdrAndColorspaceControls();

// Draw device refresh controls
void DrawDeviceRefreshControls();

} // namespace renodx::ui::new_ui
