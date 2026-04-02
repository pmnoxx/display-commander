// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/present_traffic_tracking.hpp"
#include "../../../settings/advanced_tab_settings.hpp"
#include "../../../settings/main_tab_settings.hpp"
#include "../../../utils/detour_call_tracker.hpp"
#include "../../../utils/logging.hpp"
#include "../../ui_colors.hpp"
#include "../settings_wrapper.hpp"

#include <sstream>
#include <string>

#include <Windows.h>
#include <shellapi.h>

namespace ui::new_ui {

namespace {

void DrawDxgiControl_SwapchainTweaks(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();;
    auto desc_ptr = g_last_swapchain_desc_post.load();
    const reshade::api::device_api ra = g_last_reshade_device_api.load();
    const bool is_dxgi_reshade =
        (ra == reshade::api::device_api::d3d10 || ra == reshade::api::device_api::d3d11
         || ra == reshade::api::device_api::d3d12);
    const std::string traffic_apis = display_commanderhooks::GetPresentTrafficApisString();
    const bool has_dxgi_traffic = traffic_apis.find("DXGI") != std::string::npos;

    if (g_reshade_module != nullptr) {
        if (is_dxgi_reshade) {
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
            if (ComboSettingWrapper(settings::g_mainTabSettings.max_frame_latency_override, "Max frame latency", imgui,
                                    300.f)) {
                LogInfo("Max frame latency override changed to %d",
                        settings::g_mainTabSettings.max_frame_latency_override.GetValue());
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Override IDXGISwapChain2::SetMaximumFrameLatency. No override = "
                    "game default. 1 = lowest input latency (single frame queue); 2-16 = more CPU-GPU parallelism. "
                    "Applied per swapchain at runtime.");
            }
        }
        if (!desc_ptr) {
            imgui.TextColored(ui::colors::TEXT_DIMMED,
                              "Max frame latency and buffer count appear when swapchain information is available.");
            return;
        }

        bool is_flip_model = false;
        if (is_dxgi_reshade && desc_ptr
            && (desc_ptr->present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                || desc_ptr->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)) {
            is_flip_model = true;
        }

        if (ComboSettingWrapper(settings::g_mainTabSettings.buffer_count_override, "Buffer count", imgui, 300.f)) {
            MarkRestartNeededVsyncTearing();
            LogInfo("Buffer count override changed to %d", settings::g_mainTabSettings.buffer_count_override.GetValue());
        }
        imgui.SameLine();
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Current: %d", desc_ptr->back_buffer_count);
        if (imgui.IsItemHovered()) {
            std::ostringstream tooltip;
            tooltip << "Override swapchain buffer count at creation (requires restart). No override = game default.\n"
                    << "Current: " << desc_ptr->back_buffer_count
                    << ". DXGI flip-model swap chains should use at least 3 buffers.";
            imgui.SetTooltipEx("%s", tooltip.str().c_str());
        }
        const int buffer_override = settings::g_mainTabSettings.buffer_count_override.GetValue();
        if (is_flip_model && (buffer_override == 1)) {
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_WARNING, "DXGI flip swapchain requires at least 2 buffers.");
        }
        return;
    }

    if (has_dxgi_traffic) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.max_frame_latency_override, "Max frame latency", imgui,
                                100.f)) {
            LogInfo("Max frame latency override changed to %d",
                    settings::g_mainTabSettings.max_frame_latency_override.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override SetMaximumFrameLatency. No override = game default. 1 = "
                "lowest latency; 2-16 = more parallelism. Applied per swapchain at runtime.");
        }
    }

    if (ComboSettingWrapper(settings::g_mainTabSettings.buffer_count_override, "Buffer count", imgui, 100.f)) {
        MarkRestartNeededVsyncTearing();
        LogInfo("Buffer count override changed to %d", settings::g_mainTabSettings.buffer_count_override.GetValue());
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Override swapchain buffer count at creation (requires restart). No override = game default. "
            "Applies when game creates swapchain.");
    }
}

static const char kDxgiFlipModelDocUrl[] =
    "https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model";

void DrawDisplaySettings_DXGI(display_commander::ui::IImGuiWrapper& imgui) {
    auto desc_pre = g_last_swapchain_desc_pre.load();
    const bool original_was_flip_sequential =
        desc_pre != nullptr && desc_pre->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    const bool upgrade_done = g_force_flip_discard_upgrade_done.load(std::memory_order_relaxed);

    if (!original_was_flip_sequential && !upgrade_done) {
        return;
    }

    ui::colors::PushHeader2Colors(&imgui);
    const bool dxgi_sub_open = imgui.CollapsingHeader("DXGI", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (dxgi_sub_open) {
        if (original_was_flip_sequential || upgrade_done) {
            bool force_discard = settings::g_mainTabSettings.force_flip_discard_upgrade.GetValue();
            if (imgui.Checkbox("Force Flip Discard upgrade", &force_discard)) {
                settings::g_mainTabSettings.force_flip_discard_upgrade.SetValue(force_discard);
                MarkRestartNeededVsyncTearing();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Game requested FLIP_SEQUENTIAL. When enabled, upgrade to FLIP_DISCARD on next swapchain "
                    "create for better frame pacing (requires restart).\n"
                    "FLIP_DISCARD allows the OS to drop queued frames and can enable DirectFlip.\n"
                    "Upgrade DONE: %s\n"
                    "Doc: %s\n"
                    "Notes: https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/"
                    "for-best-performance--use-dxgi-flip-model.md",
                    upgrade_done ? "true" : "false", kDxgiFlipModelDocUrl);
            }
        }
        if (upgrade_done) {
            imgui.TextColored(::ui::colors::TEXT_SUCCESS, "Upgrade applied (FLIP_SEQUENTIAL -> FLIP_DISCARD)");
        }
        imgui.SameLine();
        if (imgui.Button("doc")) {
            ShellExecuteA(nullptr, "open", kDxgiFlipModelDocUrl, nullptr, nullptr, SW_SHOW);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Opens: %s", kDxgiFlipModelDocUrl);
        }
        imgui.Spacing();
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
    DrawDisplaySettings_DXGI(imgui);
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
    DrawMainTabOptionalPanelTextureFiltering(imgui);
    imgui.Unindent();
}

}  // namespace ui::new_ui
