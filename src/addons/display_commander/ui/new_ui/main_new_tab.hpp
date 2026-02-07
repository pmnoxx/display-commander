#pragma once

#include <reshade_imgui.hpp>

namespace ui::new_ui {

void InitMainNewTab();

// Draw the main new tab content
void DrawMainNewTab(reshade::api::effect_runtime* runtime);

// Draw display settings section
void DrawDisplaySettings(reshade::api::effect_runtime* runtime);

// Display settings section helpers (split from DrawDisplaySettings)
void DrawDisplaySettings_DisplayAndTarget();
void DrawDisplaySettings_WindowModeAndApply();
void DrawDisplaySettings_FpsLimiterMode();
void DrawDisplaySettings_FpsAndBackground();
void DrawDisplaySettings_VSyncAndTearing();

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
// Draw single-frame timeline bar (Simulation, Render Submit, Present, etc. segments)
void DrawFrameTimelineBar();
// Compact frame timeline bar for performance overlay
void DrawFrameTimelineBarOverlay(bool show_tooltips = false);

// Draw compact frame time graph for overlay (fixed width)
void DrawFrameTimeGraphOverlay(bool show_tooltips = false);
void DrawNativeFrameTimeGraphOverlay(bool show_tooltips = false);
void DrawRefreshRateFrameTimesGraph(bool show_tooltips = false);

// Draw per-channel VU meter bars in the performance overlay (compact)
void DrawOverlayVUBars(bool show_tooltips = false);

}  // namespace ui::new_ui
