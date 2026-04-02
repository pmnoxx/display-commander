#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../../imgui_wrapper_base.hpp"
#include "../main_new_tab.hpp"

#include <reshade.hpp>

namespace ui::new_ui {

void DrawMainTabOptionalPanelsAdvancedSettingsUi(display_commander::ui::IImGuiWrapper& imgui);

void DrawMainTabOptionalPanelsInOrder(display_commander::ui::GraphicsApi api,
                                      display_commander::ui::IImGuiWrapper& imgui,
                                      reshade::api::effect_runtime* runtime);

}  // namespace ui::new_ui
