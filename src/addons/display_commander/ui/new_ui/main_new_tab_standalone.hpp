#pragma once

/**
 * Thin declarations for Main tab from standalone/independent UI only.
 * Use this instead of main_new_tab.hpp in translation units that #define ImGui
 * (e.g. cli_standalone_ui.cpp) to avoid pulling in reshade_imgui.hpp and duplicate ImGui symbols.
 */

#include "../imgui_wrapper_base.hpp"

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}  // namespace display_commander

namespace ui::new_ui {

void InitMainNewTab();
void DrawMainNewTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);

// Return current device API when ReShade is loaded; otherwise GraphicsApi::Unknown.
display_commander::ui::GraphicsApi GetGraphicsApiFromLastDeviceApi();

// Draw performance overlay content (clock, FPS, VRR, VRAM, flip, DLSS, volume, graphs, etc.).
void DrawPerformanceOverlayContent(display_commander::ui::IImGuiWrapper& imgui,
                                   display_commander::ui::GraphicsApi device_api,
                                   bool show_tooltips = true);

}  // namespace ui::new_ui
