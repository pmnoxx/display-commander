#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "ui/imgui_wrapper_base.hpp"

struct DLSSGSummary;

namespace ui::new_ui {

void DrawDLSSInfo(display_commander::ui::IImGuiWrapper& imgui, const DLSSGSummary& dlssg_summary);

}  // namespace ui::new_ui

