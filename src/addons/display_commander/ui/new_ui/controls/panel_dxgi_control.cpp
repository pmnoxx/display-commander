// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../settings/advanced_tab_settings.hpp"
#include "../../../utils/detour_call_tracker.hpp"
#include "../../ui_colors.hpp"

namespace ui::new_ui {

namespace {

void DrawDxgiControl_SwapchainTweaks(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();
    if (g_reshade_module == nullptr) {
        return;
    }
    const reshade::api::device_api ra = g_last_reshade_device_api.load();
    const bool is_dxgi_reshade =
        (ra == reshade::api::device_api::d3d10 || ra == reshade::api::device_api::d3d11
         || ra == reshade::api::device_api::d3d12);
    if (!is_dxgi_reshade) {
        return;
    }
    const bool show_checkbox = (g_show_auto_colorspace_fix_in_main_tab.load(std::memory_order_relaxed));
    bool auto_colorspace = settings::g_advancedTabSettings.auto_colorspace.GetValue();
    if (show_checkbox || !auto_colorspace) {
        if (imgui.Checkbox("HDR10 / scRGB color fix", &auto_colorspace)) {
            settings::g_advancedTabSettings.auto_colorspace.SetValue(auto_colorspace);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Sets DXGI swap chain and ReShade color space to match the back buffer: "
                "10-bit HDR10 (R10G10B10A2) -> HDR10 (ST2084), 16-bit FP (R16G16B16A16) -> scRGB (Linear). "
                "No change for 8-bit (SDR). Improves compatibility with RenoDX HDR10 mode. DirectX 11/12.");
        }
    }
}

}  // namespace

void DrawMainTabOptionalPanelDxgiControl(display_commander::ui::GraphicsApi api,
                                         display_commander::ui::IImGuiWrapper& imgui) {
    const bool api_dxgi = api == display_commander::ui::GraphicsApi::D3D10
                          || api == display_commander::ui::GraphicsApi::D3D11
                          || api == display_commander::ui::GraphicsApi::D3D12;
    if (!api_dxgi) {
        return;
    }
    imgui.Spacing();
    g_rendering_ui_section.store("ui:tab:main_new:dxgi_control", std::memory_order_release);
    ui::colors::PushHeader2Colors(&imgui);
    const bool dxgi_control_open = imgui.CollapsingHeader("DXGI Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (!dxgi_control_open) {
        return;
    }
    imgui.Indent();
    DrawDxgiControl_SwapchainTweaks(imgui);
    imgui.Unindent();
}

}  // namespace ui::new_ui
