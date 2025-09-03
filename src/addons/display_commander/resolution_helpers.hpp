#pragma once

#include <windows.h>

namespace resolution {





// Helper function to get selected resolution
bool GetSelectedResolution(int monitor_index, int resolution_index, int& out_width, int& out_height);

// Helper function to get selected refresh rate
bool GetSelectedRefreshRate(int monitor_index, int width, int height, int refresh_rate_index, float& out_refresh_rate);

// Helper function to get selected refresh rate as rational values
bool GetSelectedRefreshRateRational(int monitor_index, int width, int height, int refresh_rate_index,
                                   UINT32& out_numerator, UINT32& out_denominator);

// Helper function to apply display settings using modern API with rational refresh rates
bool ApplyDisplaySettingsModern(int monitor_index, int width, int height, UINT32 refresh_numerator, UINT32 refresh_denominator);

// Helper function to apply display settings using DXGI API with fractional refresh rates
bool ApplyDisplaySettingsDXGI(int monitor_index, int width, int height, UINT32 refresh_numerator, UINT32 refresh_denominator);

} // namespace resolution
