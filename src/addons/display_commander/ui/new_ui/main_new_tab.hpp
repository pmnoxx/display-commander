#pragma once

#include "../imgui_wrapper_base.hpp"
#include <reshade_imgui.hpp>

namespace ui::new_ui {

void InitMainNewTab();

// Draw the main new tab content (imgui unused in phase 1; for future standalone migration)
void DrawMainNewTab(reshade::api::effect_runtime* runtime, display_commander::ui::IImGuiWrapper& imgui);

// Draw display settings section
void DrawDisplaySettings(reshade::api::effect_runtime* runtime, display_commander::ui::IImGuiWrapper& imgui);

// Display settings section helpers (split from DrawDisplaySettings)
void DrawDisplaySettings_DisplayAndTarget(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_WindowModeAndApply(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_FpsLimiterMode(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_FpsAndBackground(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_VSyncAndTearing(display_commander::ui::IImGuiWrapper& imgui);

// Draw audio settings section
void DrawAudioSettings(display_commander::ui::IImGuiWrapper& imgui);

// Draw window controls section
void DrawWindowControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw ADHD Multi-Monitor Mode controls section
void DrawAdhdMultiMonitorControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw important information section (Flip State)
// When has_effect_runtime is false (e.g. standalone UI), frame timing/graphs are skipped to avoid crashes.
void DrawImportantInfo(display_commander::ui::IImGuiWrapper& imgui, bool has_effect_runtime = true);

// Draw frame time graph section
void DrawFrameTimeGraph(display_commander::ui::IImGuiWrapper& imgui);
void DrawNativeFrameTimeGraph(display_commander::ui::IImGuiWrapper& imgui);
// Draw single-frame timeline bar (Simulation, Render Submit, Present, etc. segments)
void DrawFrameTimelineBar(display_commander::ui::IImGuiWrapper& imgui);
// Compact frame timeline bar for performance overlay
void DrawFrameTimelineBarOverlay(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips = false);

// Draw compact frame time graph for overlay (fixed width)
void DrawFrameTimeGraphOverlay(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips = false);
void DrawNativeFrameTimeGraphOverlay(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips = false);
void DrawRefreshRateFrameTimesGraph(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips = false);

// Draw per-channel VU meter bars in the performance overlay (compact)
void DrawOverlayVUBars(display_commander::ui::IImGuiWrapper& imgui, bool show_tooltips = false);

}  // namespace ui::new_ui
