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

// Draw backbuffer format override section
void DrawBackbufferFormatOverride();

// Draw buffer resolution upgrade section
void DrawBufferResolutionUpgrade();

// Cleanup function to stop background threads
void CleanupExperimentalTab();

} // namespace ui::new_ui
