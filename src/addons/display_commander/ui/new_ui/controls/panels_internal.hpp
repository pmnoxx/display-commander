#pragma once

// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "../../imgui_wrapper_base.hpp"
#include "../main_new_tab.hpp"

namespace ui::new_ui {

void DrawMainTabOptionalPanelTextureFiltering(display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabOptionalPanelAudioControl(display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabOptionalPanelWindowButtons(display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabOptionalPanelInputControl(display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabOptionalPanelDlssControl(display_commander::ui::GraphicsApi api,
                                         display_commander::ui::IImGuiWrapper& imgui);
void DrawMainTabOptionalPanelDxgiControl(display_commander::ui::GraphicsApi api,
                                         display_commander::ui::IImGuiWrapper& imgui);
void MarkRestartNeededVsyncTearing();

}  // namespace ui::new_ui
