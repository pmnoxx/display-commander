// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"

namespace ui::new_ui {

void DrawMainTabOptionalPanelWindowButtons(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    DrawWindowControlButtons(imgui);
}

}  // namespace ui::new_ui
