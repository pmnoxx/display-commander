#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "ui/imgui_wrapper_base.hpp"

namespace reshade::api {
class effect_runtime;
}

namespace ui::new_ui {

void DrawDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui,
                         reshade::api::effect_runtime* runtime = nullptr);

}  // namespace ui::new_ui

