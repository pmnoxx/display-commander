#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "ui/imgui_wrapper_base.hpp"

namespace ui::new_ui {

// Draw important information section (Flip State).
// When ReShade runtime is not active (e.g. standalone UI), frame timing/graphs are skipped to avoid crashes.
void DrawImportantInfo(display_commander::ui::IImGuiWrapper& imgui);

}  // namespace ui::new_ui

