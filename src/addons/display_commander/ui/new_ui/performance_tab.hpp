#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}  // namespace ui
}  // namespace display_commander

namespace ui::new_ui {

// Performance tab functions (accepts ImGui wrapper for ReShade or independent UI)
void DrawPerformanceTab(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::new_ui
