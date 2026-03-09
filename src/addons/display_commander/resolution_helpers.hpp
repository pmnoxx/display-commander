#pragma once

#include <windows.h>

namespace resolution {

// Helper function to apply display settings using DXGI API with fractional refresh rates
bool ApplyDisplaySettingsDXGI(int monitor_index, int width, int height, UINT32 refresh_numerator,
                              UINT32 refresh_denominator);

}  // namespace resolution
