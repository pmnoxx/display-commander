#pragma once

#include <reshade_imgui.hpp>
#include "../imgui_wrapper_base.hpp"

namespace ui::new_ui {

void InitMainNewTab();

// Convert ReShade runtime to device API enum (for callers that have effect_runtime*).
display_commander::ui::GraphicsApi GetGraphicsApiFromRuntime(reshade::api::effect_runtime* runtime);

// Return current device API from g_last_reshade_device_api when ReShade is loaded; otherwise GraphicsApi::Unknown.
display_commander::ui::GraphicsApi GetGraphicsApiFromLastDeviceApi();

// Draw the main new tab content (api = device API in ReShade; GraphicsApi::Unknown in standalone).
void DrawMainNewTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);

// Draw display settings section (api = device API when in ReShade; Unknown in standalone)
void DrawDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);

// Display settings section helpers (split from DrawDisplaySettings)
void DrawDisplaySettings_DisplayAndTarget(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_WindowModeAndApply(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_FpsLimiter(display_commander::ui::IImGuiWrapper& imgui);
void DrawDisplaySettings_FpsAndBackground(
    display_commander::ui::IImGuiWrapper& imgui);  // kept for compatibility; content moved to FpsLimiter
void DrawDisplaySettings_VSyncAndTearing(display_commander::ui::IImGuiWrapper& imgui);

// Draw audio settings section
void DrawAudioSettings(display_commander::ui::IImGuiWrapper& imgui);

// Draw window controls section
void DrawWindowControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw ADHD Multi-Monitor Mode controls section
void DrawAdhdMultiMonitorControls(display_commander::ui::IImGuiWrapper& imgui);

// Draw important information section (Flip State)
// When has_effect_runtime is false (e.g. standalone UI), frame timing/graphs are skipped to avoid crashes.
void DrawImportantInfo(display_commander::ui::IImGuiWrapper& imgui);

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

// Draw performance overlay content (clock, FPS, VRR, VRAM, flip, DLSS, volume, graphs, etc.).
// Use with IImGuiWrapper (ReShade or standalone). device_api used for flip status; pass GraphicsApi::Unknown when no
// device.
void DrawPerformanceOverlayContent(display_commander::ui::IImGuiWrapper& imgui,
                                   display_commander::ui::GraphicsApi device_api, bool show_tooltips = true);

}  // namespace ui::new_ui
