#pragma once

#include <deps/imgui/imgui.h>

namespace ui::new_ui {

// Initialize experimental tab
void InitExperimentalTab();

// Draw the experimental tab content
void DrawExperimentalTab();

// Draw auto-click feature section
void DrawAutoClickFeature();

// Draw mouse coordinates display section
void DrawMouseCoordinatesDisplay();

// Cleanup function to stop background threads
void CleanupExperimentalTab();

} // namespace ui::new_ui
