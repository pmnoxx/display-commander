// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../settings/main_tab_settings.hpp"
#include "../../../utils/logging.hpp"
#include "../settings_wrapper.hpp"
#include "../../forkawesome.h"
#include "../../ui_colors.hpp"

namespace ui::new_ui {

void DrawMainTabOptionalPanelTextureFiltering(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:texture_filtering", std::memory_order_release);
    ui::colors::PushHeaderColors(&imgui);
    const bool texture_filtering_open = imgui.CollapsingHeader("Texture Filtering", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (texture_filtering_open) {
        imgui.Indent();

        if (SliderIntSetting(settings::g_mainTabSettings.max_anisotropy, "Anisotropic Level", "%d", imgui)) {
            LogInfo("Max anisotropy set to %d", settings::g_mainTabSettings.max_anisotropy.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override maximum anisotropic filtering level (1-16) for existing anisotropic filters.\n"
                "Set to 0 (Game default) to preserve the game's original AF settings.\n"
                "Only affects samplers that already use anisotropic filtering.");
        }

        imgui.Spacing();

        float lod_bias = settings::g_mainTabSettings.force_mipmap_lod_bias.GetValue();
        if (imgui.SliderFloat("Mipmap LOD Bias", &lod_bias, -5.0f, 5.0f, lod_bias == 0.0f ? "Game Default" : "%.2f")) {
            settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(lod_bias);
            LogInfo("Mipmap LOD bias set to %.2f", lod_bias);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Use a small (i.e. -0.6'ish) negative LOD bias to sharpen DLSS and FSR games");
        }

        if (lod_bias != 0.0f) {
            imgui.SameLine();
            if (imgui.Button("Game Default##Mipmap LOD Bias")) {
                settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(0.0f);
                LogInfo("Mipmap LOD bias reset to game default");
            }
        }

        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Game restart may be required for changes to take full effect.");

        imgui.Unindent();
    }
}

}  // namespace ui::new_ui
