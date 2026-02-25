#pragma once

namespace display_commander {
namespace ui {
struct IImGuiWrapper;
}
}

namespace ui::new_ui {

// Draw Streamline tab content (uses ImGui wrapper for ReShade or standalone UI)
void DrawStreamlineTab(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::new_ui
