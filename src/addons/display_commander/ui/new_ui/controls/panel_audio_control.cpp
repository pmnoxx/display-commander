// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../modules/module_registry.hpp"
#include "../../ui_colors.hpp"

namespace ui::new_ui {

void DrawMainTabOptionalPanelAudioControl(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:audio", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool audio_control_open = imgui.CollapsingHeader("Audio Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (audio_control_open) {
        imgui.Indent();
        modules::DrawModuleTabById("audio", imgui, nullptr);
        imgui.Unindent();
    }
}

}  // namespace ui::new_ui
