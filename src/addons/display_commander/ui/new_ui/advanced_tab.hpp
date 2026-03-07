#pragma once

#include "../../ui/imgui_wrapper_base.hpp"

namespace ui::new_ui {

void InitAdvancedTab();
void DrawAdvancedTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);

// 32/64-bit injection service status indicators (green=running, red=stopped). Right-aligned row.
// include_version_in_tooltip: when true, tooltips include addon version (e.g. for Games tab).
void DrawDcServiceStatusIndicators(display_commander::ui::IImGuiWrapper& imgui,
                                   bool include_version_in_tooltip = false);

// Inline variant: draws [32] [64] on the current line (call after SameLine). Uses dim green when addon not found.
void DrawDcServiceIndicatorsOnLine(display_commander::ui::IImGuiWrapper& imgui, bool include_version_in_tooltip = true);

}  // namespace ui::new_ui
