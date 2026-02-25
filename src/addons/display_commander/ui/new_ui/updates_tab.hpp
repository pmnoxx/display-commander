#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}

namespace ui::new_ui {

// Draw the updates tab content (uses ImGui wrapper for ReShade or standalone UI)
void DrawUpdatesTab(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::new_ui
