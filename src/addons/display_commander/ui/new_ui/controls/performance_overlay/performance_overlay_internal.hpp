#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "globals.hpp"
#include "settings/advanced_tab_settings.hpp"
#include "settings/experimental_tab_settings.hpp"
#include "settings/main_tab_settings.hpp"
#include "swapchain_events.hpp"
#include "ui/new_ui/main_new_tab.hpp"
#include "ui/ui_colors.hpp"
#include "utils/detour_call_tracker.hpp"
#include "utils/logging.hpp"
#include "utils/perf_measurement.hpp"

// Libraries <ReShade / ImGui>
#include <imgui.h>

// Libraries <C++>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

// Libraries <Windows>
#include <Windows.h>

namespace ui::new_ui::performance_overlay {

// Internal helpers shared between overlay translation units.
// Keep this minimal: prefer static helpers in owning .cpp.

// Updates/reads the frame timeline cache used by both full UI and overlay.
void UpdateFrameTimelineCache();

}  // namespace ui::new_ui::performance_overlay
