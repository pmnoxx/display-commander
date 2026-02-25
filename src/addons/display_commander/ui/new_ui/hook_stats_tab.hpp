#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}

namespace ui::new_ui {

// Hook statistics tab (uses ImGui wrapper for ReShade or standalone UI)
void DrawHookStatsTab(display_commander::ui::IImGuiWrapper& imgui);

} // namespace ui::new_ui
