#pragma once

#include <reshade_imgui.hpp>

namespace ui::new_ui {

void InitAdvancedTab();
void DrawAdvancedTab(reshade::api::effect_runtime* runtime);

}  // namespace ui::new_ui
