#pragma once

#include <imgui.h>

namespace ui::new_ui {

// Draw the developer new tab content
void DrawDeveloperNewTab();

// Draw developer settings section
void DrawDeveloperSettings();

// Draw HDR and colorspace settings section
void DrawHdrColorspaceSettings();

// Draw NVAPI settings section
void DrawNvapiSettings();

// Draw resolution override settings section (Experimental)
void DrawResolutionOverrideSettings();

// Draw keyboard shortcuts settings section
void DrawKeyboardShortcutsSettings();

// Draw latency display section
void DrawLatencyDisplay();

} // namespace ui::new_ui
