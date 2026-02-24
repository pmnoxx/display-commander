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

}  // namespace ui::new_ui
