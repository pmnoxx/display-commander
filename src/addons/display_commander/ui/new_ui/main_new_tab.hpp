#pragma once

#include <reshade_imgui.hpp>

namespace ui::new_ui {

void InitMainNewTab();

// Draw the main new tab content
void DrawMainNewTab(reshade::api::effect_runtime* runtime);

// Draw display settings section
void DrawDisplaySettings(reshade::api::effect_runtime* runtime);

// Draw audio settings section
void DrawAudioSettings();

// Draw window controls section
void DrawWindowControls();

// Draw ADHD Multi-Monitor Mode controls section
void DrawAdhdMultiMonitorControls(bool hasBlackCurtainSetting);

// Draw important information section (Flip State)
void DrawImportantInfo();

// Draw frame time graph section
void DrawFrameTimeGraph();
void DrawNativeFrameTimeGraph();

// Draw compact frame time graph for overlay (fixed width)
void DrawFrameTimeGraphOverlay(bool show_tooltips = false);
void DrawNativeFrameTimeGraphOverlay(bool show_tooltips = false);
void DrawRefreshRateFrameTimesGraph(bool show_tooltips = false);

}  // namespace ui::new_ui
