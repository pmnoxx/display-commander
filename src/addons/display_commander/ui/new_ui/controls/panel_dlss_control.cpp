// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "panels_internal.hpp"
#include "../../../globals.hpp"
#include "../../../hooks/nvidia/ngx_hooks.hpp"
#include "../../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../../hooks/vulkan/vulkan_loader_hooks.hpp"
#include "../../../settings/main_tab_settings.hpp"
#include "../../../settings/streamline_tab_settings.hpp"
#include "../../../settings/swapchain_tab_settings.hpp"
#include "../../../utils/general_utils.hpp"
#include "../../../utils/logging.hpp"
#include "../../forkawesome.h"
#include "../../ui_colors.hpp"
#include "../settings_wrapper.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <Windows.h>
#include <shellapi.h>

namespace ui::new_ui {

void DrawMainTabOptionalPanelDlssControl(display_commander::ui::GraphicsApi api,
                                         display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    const bool is_dxgi = api == display_commander::ui::GraphicsApi::D3D10
                         || api == display_commander::ui::GraphicsApi::D3D11
                         || api == display_commander::ui::GraphicsApi::D3D12;
    g_rendering_ui_section.store("ui:tab:main_new:dlss_control", std::memory_order_release);
    ui::colors::PushHeader2Colors(&imgui);
    const bool dlss_control_open = imgui.CollapsingHeader("DLSS Control", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (!dlss_control_open) {
        return;
    }
    imgui.Indent();
    if (!ShouldShowDlssInformationSection()) {
        imgui.TextColored(ui::colors::TEXT_DIMMED,
                          "No DLSS / NGX activity detected yet (no nvngx DLL loaded and no DLSS feature seen this "
                          "session).");
        imgui.Unindent();
        return;
    }
    const DLSSGSummary dlss_summary = GetDLSSGSummary();
    if (is_dxgi) {
        if (!AreNGXParameterVTableHooksInstalled()) {
            imgui.TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                              ICON_FK_WARNING " NGX Parameter vtable hooks were never installed.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "This is usually caused by ReShade loading Display Commander too late (e.g. _nvngx.dll was "
                    "already loaded). "
                    "Recommendation: use Display Commander as dxgi.dll/d3d12.dll/d3d11.dll/version.dll and "
                    "ReShade "
                    "as Reshade64.dll so our hooks are active before NGX loads. "
                    "Parameter overrides and DLSS preset controls may not apply until then.");
            }
        }
    } else if (api == display_commander::ui::GraphicsApi::Vulkan) {
        const bool nvll_loaded = (GetModuleHandleW(L"NvLowLatencyVk.dll") != nullptr);
        if (nvll_loaded) {
            if (AreNvLowLatencyVkHooksInstalled()) {
                imgui.TextColored(ui::colors::ICON_POSITIVE, ICON_FK_OK " NvLowLatencyVk.dll loaded (hooks active)");
            } else {
                imgui.TextColored(ui::colors::ICON_POSITIVE, ICON_FK_OK " NvLowLatencyVk.dll loaded");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "NvLowLatencyVk status. Hooks install when the NvLowLatencyVk option is enabled in your saved "
                    "Display Commander configuration.");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "NvLowLatencyVk.dll not loaded");
        }
        const bool vk_loaded = (GetModuleHandleW(L"vulkan-1.dll") != nullptr);
        if (vk_loaded) {
            if (AreVulkanLoaderHooksInstalled()) {
                imgui.TextColored(ui::colors::ICON_POSITIVE, ICON_FK_OK " vulkan-1.dll loaded (hooks active)");
            } else {
                imgui.TextColored(ui::colors::ICON_POSITIVE, ICON_FK_OK " vulkan-1.dll loaded");
                imgui.SameLine();
                imgui.TextColored(ui::colors::TEXT_DIMMED, "(hooks not installed)");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Vulkan loader status. Hooks install when the vulkan-1 loader hook option is enabled in your "
                    "saved Display Commander configuration.");
            }
        } else {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "vulkan-1.dll not loaded");
        }
    }
    if (settings::g_mainTabSettings.show_fps_limiter_src.GetValue()) {
        const char* src_name = GetChosenFpsLimiterSiteName();
        imgui.Text("FPS limiter source: %s", src_name);
    }

    DrawDLSSInfo(imgui, dlss_summary);

    {
        HWND hwnd = g_last_swapchain_hwnd.load();
        const bool can_send = (hwnd != nullptr && IsWindow(hwnd));
        if (!can_send) {
            imgui.BeginDisabled();
        }
        imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
        if (imgui.Button("Send WM_SIZE (force resize / recreate DLSS)")) {
            RECT client_rect = {};
            if (GetClientRect(hwnd, &client_rect)) {
                const int w = client_rect.right - client_rect.left;
                const int h = client_rect.bottom - client_rect.top;
                if (w > 0 && h > 0) {
                    LogInfo("Posted WM_SIZE w-1,h-1 then will post %dx%d after short delay", w, h);
                    std::thread([hwnd, w, h]() {
                        PostMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                    MAKELPARAM(static_cast<UINT>(w - 1), static_cast<UINT>(h - 1)));
                        Sleep(100);
                        if (IsWindow(hwnd)) {
                            PostMessage(hwnd, WM_SIZE, SIZE_RESTORED,
                                        MAKELPARAM(static_cast<UINT>(w), static_cast<UINT>(h)));
                            LogInfo("Posted WM_SIZE %dx%d to game window", w, h);
                        }
                    }).detach();
                }
            }
        }
        imgui.PopStyleColor();
        if (!can_send) {
            imgui.EndDisabled();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Sends two WM_SIZE messages: first with -1,-1, then after a short delay with the current "
                "client size. Use this to force the game to process a resize and recreate the DLSS feature.");
        }

        if (imgui.Button("Resize window to quarter then restore")) {
            RECT window_rect = {};
            if (GetWindowRect(hwnd, &window_rect)) {
                const int x = window_rect.left;
                const int y = window_rect.top;
                const int ww = window_rect.right - window_rect.left;
                const int wh = window_rect.bottom - window_rect.top;
                if (ww > 0 && wh > 0) {
                    std::thread([hwnd, x, y, ww, wh]() {
                        if (!IsWindow(hwnd)) {
                            return;
                        }
                        SetWindowPos(hwnd, nullptr, x, y, ww - 1, wh - 1, SWP_NOZORDER);
                        Sleep(100);
                        if (IsWindow(hwnd)) {
                            SetWindowPos(hwnd, nullptr, x, y, ww, wh, SWP_NOZORDER);
                            LogInfo("Resize window: quarter then restored to %dx%d", ww, wh);
                        }
                    }).detach();
                }
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Actually resizes the game window to quarter size (half width, half height), waits 150 ms, "
                "then restores the previous size. The system sends real WM_SIZE messages, which can force "
                "the game to recreate the swap chain and DLSS feature.");
        }
    }

    {
        static float s_dlss_scale_ui = -1.f;
        if (s_dlss_scale_ui < 0.f) {
            s_dlss_scale_ui = settings::g_swapchainTabSettings.dlss_internal_resolution_scale.GetValue();
        }
        imgui.SetNextItemWidth(120.0f);
        imgui.SliderFloat("Internal/Output ratio override (WIP Experimental)", &s_dlss_scale_ui, 0.0f, 1.0f, "%.2f");
        if (!imgui.IsItemActive() && !imgui.IsItemDeactivatedAfterEdit()) {
            s_dlss_scale_ui = settings::g_swapchainTabSettings.dlss_internal_resolution_scale.GetValue();
        }
        if (imgui.IsItemDeactivatedAfterEdit()) {
            settings::g_swapchainTabSettings.dlss_internal_resolution_scale.SetValue(s_dlss_scale_ui);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS internal-to-output resolution ratio. 0 = no override. e.g. 0.5 = half "
                "internal width/height vs output "
                "(OutWidth = Width * 0.5, OutHeight = Height * 0.5).");
        }
    }

    {
        static const char* dlss_quality_preset_items[] = {
            "Game Default", "Performance", "Balanced", "Quality", "Ultra Performance", "Ultra Quality", "DLAA"};
        std::string current_quality = settings::g_swapchainTabSettings.dlss_quality_preset_override.GetValue();
        int current_quality_index = 0;
        for (int i = 0; i < 7; ++i) {
            if (current_quality == dlss_quality_preset_items[i]) {
                current_quality_index = i;
                break;
            }
        }
        imgui.SetNextItemWidth(300.0f);
        if (imgui.Combo("DLSS Quality Preset Override", &current_quality_index, dlss_quality_preset_items, 7)) {
            settings::g_swapchainTabSettings.dlss_quality_preset_override.SetValue(
                dlss_quality_preset_items[current_quality_index]);
            ResetNGXPresetInitialization();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Override DLSS quality preset (Performance, Balanced, Quality, etc.). Game Default = no "
                "override. This is the quality mode, not the render preset (A, B, C).");
        }
    }

    bool dlss_override_enabled = settings::g_streamlineTabSettings.dlss_override_enabled.GetValue();
    if (imgui.Checkbox("Use DLSS override", &dlss_override_enabled)) {
        settings::g_streamlineTabSettings.dlss_override_enabled.SetValue(dlss_override_enabled);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Load DLSS DLLs from Display Commander\\dlss_override subfolders. Each DLL has its own checkbox "
            "and subfolder.");
    }
    if (dlss_override_enabled) {
        std::vector<std::string> subfolders = GetDlssOverrideSubfolderNames();
        auto draw_dll_row = [&subfolders, &imgui](const char* label, bool* p_check,
                                                  ui::new_ui::StringSetting& subfolder_setting, int dll_index) {
            imgui.Checkbox(label, p_check);
            std::string current_sub = subfolder_setting.GetValue();
            int current_index = -1;
            if (!current_sub.empty()) {
                for (size_t i = 0; i < subfolders.size(); ++i) {
                    if (subfolders[i] == current_sub) {
                        current_index = static_cast<int>(i);
                        break;
                    }
                }
            }
            const char* combo_label = (current_index >= 0)
                                          ? subfolders[static_cast<size_t>(current_index)].c_str()
                                      : current_sub.empty() ? "(root folder)"
                                                            : current_sub.c_str();
            imgui.SameLine();
            imgui.SetNextItemWidth(200.0f);
            if (imgui.BeginCombo((std::string("##dlss_sub_") + std::to_string(dll_index)).c_str(),
                                 combo_label)) {
                if (imgui.Selectable("(root folder)", current_sub.empty())) {
                    subfolder_setting.SetValue("");
                }
                for (size_t i = 0; i < subfolders.size(); ++i) {
                    bool selected = (current_index == static_cast<int>(i));
                    if (imgui.Selectable(subfolders[i].c_str(), selected)) {
                        subfolder_setting.SetValue(subfolders[i]);
                    }
                }
                imgui.EndCombo();
            }
            {
                std::string effective_folder = GetEffectiveDefaultDlssOverrideFolder(current_sub).string();
                DlssOverrideDllStatus st =
                    GetDlssOverrideFolderDllStatus(effective_folder, (dll_index == 0), (dll_index == 1), (dll_index == 2));
                if (st.dlls.size() > static_cast<size_t>(dll_index)) {
                    const DlssOverrideDllEntry& e = st.dlls[dll_index];
                    imgui.SameLine();
                    if (e.present) {
                        imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", e.version.c_str());
                    } else {
                        imgui.TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), "Missing");
                    }
                }
            }
        };
        bool dlss_on = settings::g_streamlineTabSettings.dlss_override_dlss.GetValue();
        bool dlss_fg_on = settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue();
        bool dlss_rr_on = settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue();
        draw_dll_row("nvngx_dlss.dll (DLSS)##main", &dlss_on, settings::g_streamlineTabSettings.dlss_override_subfolder, 0);
        if (dlss_on != settings::g_streamlineTabSettings.dlss_override_dlss.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss.SetValue(dlss_on);
        }
        draw_dll_row("nvngx_dlssd.dll (D = denoiser / RR)##main", &dlss_rr_on,
                     settings::g_streamlineTabSettings.dlss_override_subfolder_dlssd, 1);
        if (dlss_rr_on != settings::g_streamlineTabSettings.dlss_override_dlss_rr.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss_rr.SetValue(dlss_rr_on);
        }
        draw_dll_row("nvngx_dlssg.dll (G = generation / FG)##main", &dlss_fg_on,
                     settings::g_streamlineTabSettings.dlss_override_subfolder_dlssg, 2);
        if (dlss_fg_on != settings::g_streamlineTabSettings.dlss_override_dlss_fg.GetValue()) {
            settings::g_streamlineTabSettings.dlss_override_dlss_fg.SetValue(dlss_fg_on);
        }
        static char dlss_add_folder_buf[128] = "";
        imgui.SetNextItemWidth(120.0f);
        imgui.InputTextWithHint("##dlss_add_folder", "e.g. 310.5.2", dlss_add_folder_buf, sizeof(dlss_add_folder_buf));
        imgui.SameLine();
        if (imgui.Button("Add Folder")) {
            std::string name(dlss_add_folder_buf);
            std::string err;
            if (CreateDlssOverrideSubfolder(name, &err)) {
                dlss_add_folder_buf[0] = '\0';
            } else if (!err.empty()) {
                LogError("DLSS override Add Folder: %s", err.c_str());
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Create subfolder under Display Commander\\dlss_override.");
        }
    }
    imgui.SameLine();
    imgui.PushStyleColor(ImGuiCol_Text, ui::colors::ICON_ACTION);
    if (imgui.Button(ICON_FK_FOLDER_OPEN " Open DLSS override folder")) {
        std::string folder_to_open = GetDefaultDlssOverrideFolder().string();
        std::thread([folder_to_open]() {
            std::error_code ec;
            std::filesystem::create_directories(folder_to_open, ec);
            if (ec) {
                LogError("Failed to create DLSS override folder: %s (%s)", folder_to_open.c_str(), ec.message().c_str());
                return;
            }
            HINSTANCE result = ShellExecuteA(nullptr, "explore", folder_to_open.c_str(), nullptr, nullptr, SW_SHOW);
            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open DLSS override folder: %s (Error: %ld)", folder_to_open.c_str(),
                         reinterpret_cast<intptr_t>(result));
            }
        }).detach();
    }
    imgui.PopStyleColor();
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the folder where you can place custom DLSS DLLs (created if missing).");
    }

    imgui.Unindent();
}

}  // namespace ui::new_ui
