#pragma once

#include <imgui.h>

namespace ui::new_ui {

void InitMainNewTab();

void InitDeveloperNewTab();

// Draw the main new tab content
void DrawMainNewTab();

// Draw display settings section
void DrawDisplaySettings();

// Draw monitor/display resolution settings section
void DrawMonitorDisplaySettings();

// Draw audio settings section
void DrawAudioSettings();

// Draw window controls section
void DrawWindowControls();

// Draw ADHD Multi-Monitor Mode controls section
void DrawAdhdMultiMonitorControls();

// Draw important information section (Flip State)
void DrawImportantInfo();

} // namespace ui::new_ui
