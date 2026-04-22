// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "display_settings_internal.hpp"
#include "presentmon_minimal_flip_state_row.hpp"
#include "vsync_tearing.hpp"

namespace ui::new_ui {

namespace {

// Flag to indicate a restart is required after changing VSync/tearing options
std::atomic<bool> s_restart_needed_vsync_tearing{false};

// Context for VSync & Tearing swapchain debug tooltip (filled by PresentModeLine, consumed by SwapchainTooltip).
// desc_holder keeps the swapchain desc alive for the tooltip duration to avoid use-after-free if
// g_last_swapchain_desc_post is updated (e.g. swapchain recreated) while the tooltip is open.
struct VSyncTearingTooltipContext {
    std::shared_ptr<reshade::api::swapchain_desc> desc_holder;
    const reshade::api::swapchain_desc* desc = nullptr;
    std::string present_mode_name;
};

/// Returns present mode display name for non-DXGI APIs (Vulkan, OpenGL).
/// ReShade: present_mode is VkPresentModeKHR for Vulkan, WGL_SWAP_METHOD_ARB for OpenGL.
static const char* GetPresentModeNameNonDxgi(int device_api_value, uint32_t present_mode) {
    if (device_api_value == static_cast<int>(reshade::api::device_api::vulkan)) {
        switch (present_mode) {
            case 0:  return "IMMEDIATE (Vulkan)";
            case 1:  return "MAILBOX (Vulkan)";
            case 2:  return "FIFO (Vulkan)";
            case 3:  return "FIFO_RELAXED (Vulkan)";
            default: return "VkPresentMode (Vulkan)";
        }
    }
    if (device_api_value == static_cast<int>(reshade::api::device_api::opengl)) {
        return "OpenGL";
    }
    return "Other";
}

static void DrawDisplaySettings_VSyncAndTearing_Checkboxes_Reshade(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();
    const reshade::api::device_api current_api_pt = g_last_reshade_device_api.load();
    const bool is_dxgi_pt =
        (current_api_pt == reshade::api::device_api::d3d10 || current_api_pt == reshade::api::device_api::d3d11
         || current_api_pt == reshade::api::device_api::d3d12);
    if (g_reshade_create_swapchain_capture_count.load() > 0) {
        auto desc_ptr_cb = g_last_swapchain_desc_post.load();
        if (is_dxgi_pt) {
            PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui), true);
            if (ComboSettingWrapper(settings::g_mainTabSettings.vsync_override, "VSync", imgui, 300.f)) {
                LogInfo("VSync override changed to index %d", settings::g_mainTabSettings.vsync_override.GetValue());
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Override DXGI Present SyncInterval. No override = use game setting. Force ON = VSync every "
                    "frame; 1/2-1/4 = every 2nd-4th vblank (not VRR); FORCED OFF = no VSync. Applied at runtime (no "
                    "restart).");
            }

            imgui.SameLine();
            bool prevent_t = settings::g_mainTabSettings.prevent_tearing.GetValue();
            if (imgui.Checkbox("Prevent Tearing", &prevent_t)) {
                settings::g_mainTabSettings.prevent_tearing.SetValue(prevent_t);
                LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)"
                                  : "Prevent Tearing disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Prevents tearing by clearing DXGI tearing flags and preferring sync.");
            }
        } else {
            bool vs_on = settings::g_mainTabSettings.force_vsync_on.GetValue();
            if (imgui.Checkbox("Force VSync ON", &vs_on)) {
                s_restart_needed_vsync_tearing.store(true);
                if (vs_on) {
                    settings::g_mainTabSettings.force_vsync_off.SetValue(false);
                }
                settings::g_mainTabSettings.force_vsync_on.SetValue(vs_on);
                LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Forces sync interval = 1 (requires restart).");
            }
            imgui.SameLine();

            bool vs_off = settings::g_mainTabSettings.force_vsync_off.GetValue();
            if (imgui.Checkbox("Force VSync OFF", &vs_off)) {
                s_restart_needed_vsync_tearing.store(true);
                if (vs_off) {
                    settings::g_mainTabSettings.force_vsync_on.SetValue(false);
                }
                settings::g_mainTabSettings.force_vsync_off.SetValue(vs_off);
                LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Forces sync interval = 0 (requires restart).");
            }
        }
        if (is_dxgi_pt) {
        } else if (desc_ptr_cb) {
            imgui.SameLine();
            imgui.TextColored(ui::colors::TEXT_DIMMED, "Present mode: %s",
                              GetPresentModeNameNonDxgi(static_cast<int>(current_api_pt), desc_ptr_cb->present_mode));
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Current swapchain present mode (Vulkan: VkPresentModeKHR, OpenGL: WGL). Read-only.");
            }
        }
    } else {
        if ((g_reshade_module != nullptr)) {
            imgui.TextColored(ui::colors::TEXT_WARNING,
                              "VSYNC ON/OFF Prevent Tearing options unavailable due to reshade bug!");
        }
    }

    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    const bool is_d3d9 = current_api == reshade::api::device_api::d3d9;
    const bool is_dxgi =
        (current_api == reshade::api::device_api::d3d10 || current_api == reshade::api::device_api::d3d11
         || current_api == reshade::api::device_api::d3d12);
    bool enable_flip = settings::g_advancedTabSettings.enable_flip_chain.GetValue();
    bool is_flip = false;
    if (is_dxgi) {
        auto desc_for_flip = g_last_swapchain_desc_post.load();
        if (desc_for_flip
            && (desc_for_flip->present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                || desc_for_flip->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)) {
            is_flip = true;
        }
    }
    static bool has_been_enabled = false;
    has_been_enabled |= is_dxgi && (enable_flip || !is_flip);

    if (has_been_enabled) {
        imgui.SameLine();
        if (imgui.Checkbox("Enable Flip Chain (requires restart)", &enable_flip)) {
            settings::g_advancedTabSettings.enable_flip_chain.SetValue(enable_flip);
            s_restart_needed_vsync_tearing.store(true);
            LogInfo(enable_flip ? "Enable Flip Chain enabled" : "Enable Flip Chain disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
                "This setting requires a game restart to take effect.\n"
                "Only works with DirectX 10/11/12 (DXGI) games.");
        }
    }

    if (is_d3d9) {
        imgui.SameLine();
        bool enable_d9ex_with_flip = settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue();
        if (imgui.Checkbox("Enable Flip State (requires restart)", &enable_d9ex_with_flip)) {
            settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(enable_d9ex_with_flip);
            LogInfo(enable_d9ex_with_flip ? "Enable D9EX with Flip Model enabled"
                                          : "Enable D9EX with Flip Model disabled");
        }
    }

    if (s_restart_needed_vsync_tearing.load()) {
        imgui.Spacing();
        imgui.TextColored(ui::colors::TEXT_ERROR, "Game restart required to apply VSync/tearing changes.");
    }

    imgui.Spacing();
}

static void DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(display_commander::ui::IImGuiWrapper& imgui,
                                                                 const VSyncTearingTooltipContext& ctx) {
    (void)imgui;
    CALL_GUARD_NO_TS();
    if (ctx.desc == nullptr) return;
    const auto& desc = *ctx.desc;
    const reshade::api::device_api api_val = g_last_reshade_device_api.load();

    imgui.TextColored(ui::colors::TEXT_LABEL, "Swapchain Information:");
    imgui.Separator();
    imgui.Text("Present Mode: %s", ctx.present_mode_name.c_str());
    imgui.Text("Present Mode ID: %u", desc.present_mode);
    if (api_val == reshade::api::device_api::vulkan) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "Vulkan swapchain (VkPresentModeKHR, flags below)");
    } else if (api_val == reshade::api::device_api::opengl) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "OpenGL swapchain");
    } else if (api_val == reshade::api::device_api::d3d10 || api_val == reshade::api::device_api::d3d11
               || api_val == reshade::api::device_api::d3d12) {
        imgui.TextColored(ui::colors::TEXT_DIMMED, "DXGI swapchain");
    }

    HWND game_window = display_commanderhooks::GetGameWindow();
    if (game_window != nullptr && IsWindow(game_window)) {
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Window Information (Debug):");
        RECT window_rect = {};
        RECT client_rect = {};
        if (GetWindowRect(game_window, &window_rect) && GetClientRect(game_window, &client_rect)) {
            imgui.Text("Window Rect: (%ld, %ld) to (%ld, %ld)", window_rect.left, window_rect.top, window_rect.right,
                       window_rect.bottom);
            imgui.Text("Window Size: %ld x %ld", window_rect.right - window_rect.left,
                       window_rect.bottom - window_rect.top);
            imgui.Text("Client Rect: (%ld, %ld) to (%ld, %ld)", client_rect.left, client_rect.top, client_rect.right,
                       client_rect.bottom);
            imgui.Text("Client Size: %ld x %ld", client_rect.right - client_rect.left,
                       client_rect.bottom - client_rect.top);
        }
        LONG_PTR style = GetWindowLongPtrW(game_window, GWL_STYLE);
        LONG_PTR ex_style = GetWindowLongPtrW(game_window, GWL_EXSTYLE);
        imgui.Text("Window Style: 0x%08lX", static_cast<unsigned long>(style));
        imgui.Text("Window ExStyle: 0x%08lX", static_cast<unsigned long>(ex_style));
        bool is_popup = (style & WS_POPUP) != 0;
        bool is_child = (style & WS_CHILD) != 0;
        bool has_caption = (style & WS_CAPTION) != 0;
        bool has_border = (style & WS_BORDER) != 0;
        bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
        bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
        bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;
        imgui.Text("  WS_POPUP: %s", is_popup ? "Yes" : "No");
        imgui.Text("  WS_CHILD: %s", is_child ? "Yes" : "No");
        imgui.Text("  WS_CAPTION: %s", has_caption ? "Yes" : "No");
        imgui.Text("  WS_BORDER: %s", has_border ? "Yes" : "No");
        imgui.Text("  WS_EX_LAYERED: %s", is_layered ? "Yes" : "No");
        imgui.Text("  WS_EX_TOPMOST: %s", is_topmost ? "Yes" : "No");
        imgui.Text("  WS_EX_TRANSPARENT: %s", is_transparent ? "Yes" : "No");
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Size Comparison:");
        imgui.Text("Backbuffer: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
        if (GetWindowRect(game_window, &window_rect)) {
            long window_width = window_rect.right - window_rect.left;
            long window_height = window_rect.bottom - window_rect.top;
            imgui.Text("Window: %ldx%ld", window_width, window_height);
            bool size_matches = (static_cast<long>(desc.back_buffer.texture.width) == window_width
                                 && static_cast<long>(desc.back_buffer.texture.height) == window_height);
            if (size_matches) {
                imgui.TextColored(ui::colors::TEXT_SUCCESS, "  Sizes match");
            } else {
                imgui.TextColored(ui::colors::TEXT_WARNING, "  Sizes differ (may cause Composed Flip)");
            }
        }
        imgui.Separator();
        imgui.TextColored(ui::colors::TEXT_LABEL, "Display Information:");
        HMONITOR monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr) {
            MONITORINFOEXW monitor_info = {};
            monitor_info.cbSize = sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(monitor, &monitor_info)) {
                imgui.Text("Monitor Rect: (%ld, %ld) to (%ld, %ld)", monitor_info.rcMonitor.left,
                           monitor_info.rcMonitor.top, monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom);
                long monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                long monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
                imgui.Text("Monitor Size: %ld x %ld", monitor_width, monitor_height);
                if (GetWindowRect(game_window, &window_rect)) {
                    bool covers_monitor = (window_rect.left == monitor_info.rcMonitor.left
                                           && window_rect.top == monitor_info.rcMonitor.top
                                           && window_rect.right == monitor_info.rcMonitor.right
                                           && window_rect.bottom == monitor_info.rcMonitor.bottom);
                    if (covers_monitor) {
                        imgui.TextColored(ui::colors::TEXT_SUCCESS, "  Window covers entire monitor");
                    } else {
                        imgui.TextColored(ui::colors::TEXT_WARNING, "  Window does not cover entire monitor");
                    }
                }
            }
        }
    }

    imgui.Text("Back Buffer Count: %u", desc.back_buffer_count);
    imgui.Text("Back Buffer Size: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
    const char* format_name = "Unknown";
    switch (desc.back_buffer.texture.format) {
        case reshade::api::format::r10g10b10a2_unorm:  format_name = "R10G10B10A2_UNORM (HDR 10-bit)"; break;
        case reshade::api::format::r16g16b16a16_float: format_name = "R16G16B16A16_FLOAT (HDR 16-bit)"; break;
        case reshade::api::format::r8g8b8a8_unorm:     format_name = "R8G8B8A8_UNORM (SDR 8-bit)"; break;
        case reshade::api::format::b8g8r8a8_unorm:     format_name = "B8G8R8A8_UNORM (SDR 8-bit)"; break;
        default:                                       format_name = "Unknown Format"; break;
    }
    imgui.Text("Back Buffer Format: %s", format_name);
    imgui.Text("Sync Interval: %u", desc.sync_interval);
    imgui.Text("Fullscreen: %s", desc.fullscreen_state ? "Yes" : "No");
    if (desc.fullscreen_state && desc.fullscreen_refresh_rate > 0) {
        imgui.Text("Refresh Rate: %.2f Hz", desc.fullscreen_refresh_rate);
    }

    imgui.Separator();
    imgui.Spacing();
    // ReShade: present_flags is DXGI_SWAP_CHAIN_FLAG (DXGI), VkSwapchainCreateFlagsKHR (Vulkan), or PFD_* (OpenGL).
    if (desc.present_flags != 0) {
        const reshade::api::device_api api_val2 = g_last_reshade_device_api.load();
        const bool is_dxgi_flags =
            (api_val2 == reshade::api::device_api::d3d10 || api_val2 == reshade::api::device_api::d3d11
             || api_val2 == reshade::api::device_api::d3d12);
        if (is_dxgi_flags) {
            imgui.Text("Swap chain creation flags (DXGI): 0x%X", desc.present_flags);
            imgui.Text("Flags:");
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
                imgui.Text("  • ALLOW_TEARING (VRR/G-Sync)");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
                imgui.Text("  • FRAME_LATENCY_WAITABLE_OBJECT");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
                imgui.Text("  • DISPLAY_ONLY");
            }
            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
                imgui.Text("  • RESTRICTED_CONTENT");
            }
        } else if (api_val2 == reshade::api::device_api::vulkan) {
            imgui.Text("Swapchain creation flags (VkSwapchainCreateFlagsKHR): 0x%X", desc.present_flags);
        } else {
            imgui.Text("Creation flags: 0x%X", desc.present_flags);
        }
    }
}

static bool DrawDisplaySettings_VSyncAndTearing_PresentModeLine(display_commander::ui::IImGuiWrapper& imgui,
                                                                VSyncTearingTooltipContext* out_ctx) {
    CALL_GUARD_NO_TS();
    auto desc_ptr = g_last_swapchain_desc_post.load();
    if (!desc_ptr) {
        return false;
    }
    CALL_GUARD_NO_TS();
    const auto& desc = *desc_ptr;
    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    const bool is_d3d9 = current_api == reshade::api::device_api::d3d9;
    const bool is_dxgi =
        (current_api == reshade::api::device_api::d3d10 || current_api == reshade::api::device_api::d3d11
         || current_api == reshade::api::device_api::d3d12);

    PushFpsLimiterSliderColumnAlign(imgui, GetMainTabCheckboxColumnGutter(imgui), true);
    imgui.TextColored(ui::colors::TEXT_LABEL, "Current Present Mode:");
    imgui.SameLine();
    ImVec4 present_mode_color = ui::colors::TEXT_DIMMED;
    std::string present_mode_name = "Unknown";

    if (is_d3d9) {
        CALL_GUARD_NO_TS();
        if (desc.present_mode == D3DSWAPEFFECT_FLIPEX) {
            present_mode_name = "FLIPEX (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_DISCARD) {
            present_mode_name = "DISCARD (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_FLIP) {
            present_mode_name = "FLIP (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_COPY) {
            present_mode_name = "COPY (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == D3DSWAPEFFECT_OVERLAY) {
            present_mode_name = "OVERLAY (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else {
            present_mode_name = "Unknown";
            present_mode_color = ui::colors::TEXT_ERROR;
        }
        if (desc.fullscreen_state) {
            present_mode_name = present_mode_name + "(FSE)";
        }
        imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
        bool status_hovered = imgui.IsItemHovered();
        CALL_GUARD_NO_TS();
        if (out_ctx) {
            out_ctx->desc_holder = desc_ptr;
            out_ctx->desc = desc_ptr.get();
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    if (is_dxgi) {
        CALL_GUARD_NO_TS();
        if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD) {
            present_mode_name = "FLIP_DISCARD (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
            present_mode_name = "FLIP_SEQUENTIAL (Flip Model)";
            present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_DISCARD) {
            present_mode_name = "DISCARD (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else if (desc.present_mode == DXGI_SWAP_EFFECT_SEQUENTIAL) {
            present_mode_name = "SEQUENTIAL (Traditional)";
            present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        } else {
            present_mode_name = "Unknown";
            present_mode_color = ui::colors::TEXT_ERROR;
        }
        imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
        bool status_hovered = imgui.IsItemHovered();
        if (out_ctx) {
            out_ctx->desc_holder = desc_ptr;
            out_ctx->desc = desc_ptr.get();
            out_ctx->present_mode_name = std::move(present_mode_name);
        }
        return status_hovered;
    }

    // Vulkan, OpenGL, etc.: show present mode (ReShade: VkPresentModeKHR or WGL)
    CALL_GUARD_NO_TS();
    present_mode_name = GetPresentModeNameNonDxgi(static_cast<int>(current_api), desc.present_mode);
    present_mode_color = ui::colors::TEXT_DIMMED;
    imgui.TextColored(present_mode_color, "%s", present_mode_name.c_str());
    bool status_hovered = imgui.IsItemHovered();
    if (out_ctx) {
        out_ctx->desc_holder = desc_ptr;
        out_ctx->desc = desc_ptr.get();
        out_ctx->present_mode_name = std::move(present_mode_name);
    }
    return status_hovered;
}

}  // namespace

void DrawDisplaySettings_VSyncAndTearing(display_commander::ui::IImGuiWrapper& imgui) {
    CALL_GUARD_NO_TS();

    g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing", std::memory_order_release);
    ui::colors::PushHeader2Colors(&imgui);
    const bool vsync_tearing_open = imgui.CollapsingHeader("VSync & Tearing", ImGuiTreeNodeFlags_None);
    ui::colors::PopCollapsingHeaderColors(&imgui);
    if (vsync_tearing_open) {
        imgui.Indent();
        DrawDisplaySettings_VSyncAndTearing_Checkboxes_Reshade(imgui);

        VSyncTearingTooltipContext tooltip_ctx;
        bool status_hovered = DrawDisplaySettings_VSyncAndTearing_PresentModeLine(imgui, &tooltip_ctx);
        DrawPresentMonMinimalFlipStateRow(imgui);
        g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:present_mode_line", std::memory_order_release);
        if (status_hovered && tooltip_ctx.desc != nullptr) {
            imgui.BeginTooltip();
            DrawDisplaySettings_VSyncAndTearing_SwapchainTooltip(imgui, tooltip_ctx);
            imgui.EndTooltip();
        }
        g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:swapchain_tooltip", std::memory_order_release);

        if (!g_last_swapchain_desc_post.load()) {
            imgui.TextColored(ui::colors::TEXT_DIMMED, "No swapchain information available");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "No game detected or swapchain not yet created.\nThis information will appear once a game is "
                    "running.");
            }
        }
        imgui.Unindent();
    }
    g_rendering_ui_section.store("ui:tab:main_new:vsync_tearing:end", std::memory_order_release);
}

void MarkRestartNeededVsyncTearing() {
    s_restart_needed_vsync_tearing.store(true);
}

}  // namespace ui::new_ui

