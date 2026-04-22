// Source Code <Display Commander> // follow this order for includes in all files + add this comment at the top
#include "debug_tab.hpp"
#include "../../../globals.hpp"
#include "display_config_debug_tab.hpp"
#include "dxgi_refresh_rate_tab.hpp"
#include "fps_limiter_debug_tab.hpp"
#include "ngx_counters_tab.hpp"
#include "nvidia_profile_inspector_tab.hpp"
#include "presentmon_debug_tab.hpp"
#include "reflex_pclstats_tab.hpp"
#include "vulkan_tab.hpp"
#include "window_info_debug_tab.hpp"
#include "window_messages_tab.hpp"

// Libraries <ReShade> / <imgui>
#include <imgui.h>

// Libraries <standard C++>
#include <atomic>

// Libraries <Windows.h> — before other Windows headers
#include <Windows.h>

namespace ui::new_ui::debug {

void DrawDebugTab(display_commander::ui::IImGuiWrapper& imgui) {
    if (!imgui.BeginTabBar("dc_debug_subtabs", 0)) {
        return;
    }

    if (imgui.BeginTabItem("Messages", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_messages", std::memory_order_release);
        DrawWindowMessagesTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("Window info", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_window_info", std::memory_order_release);
        DrawWindowInfoDebugTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("Vulkan", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_vulkan", std::memory_order_release);
        DrawVulkanTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("DisplayConfig", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_display_config", std::memory_order_release);
        DrawDisplayConfigDebugTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("DXGI refresh", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_dxgi_refresh", std::memory_order_release);
        DrawDxgiRefreshRateTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("FPS limiter", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_fps_limiter_lite", std::memory_order_release);
        DrawFpsLimiterDebugTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("Reflex / PCLStats", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_reflex_pclstats", std::memory_order_release);
        DrawReflexPclstatsTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("PresentMon (NVAPI)", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_presentmon_nvapi", std::memory_order_release);
        DrawPresentMonDebugTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("NGX", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_ngx_counters", std::memory_order_release);
        DrawNGXCountersTab(imgui);
        imgui.EndTabItem();
    }
    if (imgui.BeginTabItem("NVIDIA profile", nullptr, 0)) {
        g_rendering_ui_section.store("ui:tab:debug_nvidia_profile", std::memory_order_release);
        DrawNvidiaProfileInspectorTab(imgui);
        imgui.EndTabItem();
    }

    imgui.EndTabBar();
}

}  // namespace ui::new_ui::debug
