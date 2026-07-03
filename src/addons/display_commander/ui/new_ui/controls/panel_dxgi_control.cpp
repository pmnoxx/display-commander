// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
// Headers <Display Commander>
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../settings/advanced_tab_settings.hpp"
#include "../../../settings/main_tab_settings.hpp"
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
    const uint32_t flip_metering_calls = g_nvapi_d3d12_setflipconfig_seen.load(std::memory_order_acquire);
    const uint32_t flip_metering_suppressions =
        g_nvapi_d3d12_setflipconfig_suppressions.load(std::memory_order_acquire);
    const bool flip_metering_seen = (flip_metering_calls > flip_metering_suppressions);
    imgui.Text("Flip Metering [rtx 5000+]:");
    imgui.SameLine();
    imgui.TextColored(flip_metering_seen ? ::ui::colors::TEXT_SUCCESS : ::ui::colors::TEXT_DIMMED,
                      flip_metering_seen ? "ON" : "OFF");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "NVAPI_D3D12_SetFlipConfig (0xF3148C42) QueryInterface:\n"
            "  Calls this session: %u\n"
            "  Successful suppressions: %u\n"
            "ON when calls exceed suppressions (game received the function pointer at least once net).",
            flip_metering_calls, flip_metering_suppressions);
    }
    imgui.SameLine();
    bool allow_flip = settings::g_mainTabSettings.allow_nvapi_d3d12_setflipconfig.GetValue();
    if (imgui.Checkbox("Allow##flip_metering_nvapi", &allow_flip)) {
        settings::g_mainTabSettings.allow_nvapi_d3d12_setflipconfig.SetValue(allow_flip);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, NvAPI_QueryInterface returns the real SetFlipConfig entry (default).\n"
            "When disabled, returns nullptr for that ID and increments the suppression counter.");
    }
    DrawDxgiControl_SwapchainTweaks(imgui);
    imgui.Unindent();
}

}  // namespace ui::new_ui
