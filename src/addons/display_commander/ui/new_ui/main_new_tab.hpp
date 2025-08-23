#pragma once

#include <deps/imgui/imgui.h>
#include <functional>

namespace renodx::ui::new_ui {

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

// Draw basic reflex settings section
void DrawBasicReflexSettings();

// Draw important information section (PCL AV Latency, Reflex Status, Flip State)
void DrawImportantInfo();

} // namespace renodx::ui::new_ui
