// Source Code <Display Commander> // Smooth Motion feature slice
#pragma once

// Libraries <standard C++>
#include <string>

namespace display_commander::features::smooth_motion {

// Marks Smooth Motion as loaded when nvpresent64.dll or nvpresent32.dll is observed in module load events.
void OnModuleLoaded(const std::wstring& module_name);

// Returns true if Smooth Motion (nvpresent) has been observed as loaded.
bool IsSmoothMotionLoaded();

}  // namespace display_commander::features::smooth_motion
