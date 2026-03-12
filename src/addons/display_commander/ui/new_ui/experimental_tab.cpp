#include "experimental_tab.hpp"
#include "../../autoclick/autoclick_manager.hpp"
#include "../../dlss/dlss_indicator_manager.hpp"
#include "../../globals.hpp"
#include "../../hooks/windows_hooks/api_hooks.hpp"
#include "../../hooks/debug_output_hooks.hpp"
#include "../../hooks/input/hid_suppression_hooks.hpp"
#include "../../hooks/hook_suppression_manager.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/rand_hooks.hpp"
#include "../../hooks/sleep_hooks.hpp"
#include "../../hooks/timeslowdown_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../nvapi/nvidia_profile_search.hpp"
#include "../../nvapi/nvpi_reference.hpp"
#include "../../res/forkawesome.h"
#include "../../res/link_libraries.hpp"
#include "../../res/ui_colors.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../ui/imgui_wrapper_reshade.hpp"
#include "../../ui/nvidia_profile_tab_shared.hpp"
#include "../../utils/d3d9_api_version.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../../widgets/dualsense_widget/dualsense_widget.hpp"
#include "../imgui_wrapper_reshade.hpp"
#include "../monitor_settings/monitor_settings.hpp"
#include "hook_stats_tab.hpp"
#include "main_new_tab.hpp"
#include "settings_wrapper.hpp"
#include "streamline_tab.hpp"
#include "swapchain_tab.hpp"
#include "updates_tab.hpp"
#include "window_info_tab.hpp"

#include <psapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>

namespace ui::new_ui {

static void DrawThreadTrackingSubTab(display_commander::ui::IImGuiWrapper& imgui);
static void DrawHooksConfigTab(display_commander::ui::IImGuiWrapper& imgui);

void DrawNvidiaProfileTab(reshade::api::effect_runtime* runtime) {
    display_commander::ui::GraphicsApi api = display_commander::ui::GraphicsApi::Unknown;
    if (runtime != nullptr && runtime->get_device() != nullptr) {
        api = static_cast<display_commander::ui::GraphicsApi>(
            static_cast<std::uint32_t>(runtime->get_device()->get_api()));
    }
    bool showAdvanced = settings::g_experimentalTabSettings.show_advanced_profile_settings.GetValue();
    display_commander::ui::ImGuiWrapperReshade wrapper;
    display_commander::ui::DrawNvidiaProfileTab(api, wrapper, &showAdvanced);
    if (showAdvanced != settings::g_experimentalTabSettings.show_advanced_profile_settings.GetValue()) {
        settings::g_experimentalTabSettings.show_advanced_profile_settings.SetValue(showAdvanced);
        settings::g_experimentalTabSettings.show_advanced_profile_settings.Save();
    }
}

// Initialize experimental tab
void InitExperimentalTab() {
    LogInfo("InitExperimentalTab() - Settings already loaded at startup");

    // Apply the loaded settings to the actual hook system
    // This ensures the hook system matches the UI settings
    LogInfo("InitExperimentalTab() - Applying loaded timer hook settings to hook system");
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::QueryPerformanceCounter,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.query_performance_counter_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetTickCount,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_tick_count_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetTickCount64,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_tick_count64_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::TimeGetTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.time_get_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTimeAsFileTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_as_file_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetSystemTimePreciseAsFileTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::GetLocalTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.get_local_time_hook.GetValue()));
    display_commanderhooks::SetTimerHookTypeById(
        display_commanderhooks::TimerHookIdentifier::NtQuerySystemTime,
        static_cast<display_commanderhooks::TimerHookType>(
            settings::g_experimentalTabSettings.nt_query_system_time_hook.GetValue()));

    // Apply DirectInput hook suppression setting
    s_suppress_dinput_hooks.store(settings::g_experimentalTabSettings.suppress_dinput_hooks.GetValue());

    // Apply thread tracking setting (for frame pacing debug)
    g_thread_tracking_enabled.store(settings::g_experimentalTabSettings.thread_tracking_enabled.GetValue(),
                                    std::memory_order_relaxed);

    LogInfo("InitExperimentalTab() - Experimental tab settings loaded and applied to hook system");
}

void DrawExperimentalTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* runtime) {
    const bool is_standalone = (runtime == nullptr);
    if (!imgui.BeginTabBar("ExperimentalSubTabs", 0)) {
        return;
    }

    if (imgui.BeginTabItem("Features", nullptr, 0)) {
        imgui.Text("Experimental Tab - Advanced Features");
        imgui.Separator();

        if (imgui.CollapsingHeader("Direct3D 9 FLIPEX Upgrade",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawD3D9FlipExControls(imgui);
        }
        imgui.Spacing();

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("Backbuffer Format Override",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawBackbufferFormatOverride(imgui);
                imgui.Spacing();
                DrawBufferResolutionUpgrade(imgui);
                imgui.Spacing();
                DrawTextureFormatUpgrade(imgui);
            }
            imgui.Spacing();

            if (imgui.CollapsingHeader("Auto-Click Sequences",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                POINT mouse_pos;
                GetCursorPos(&mouse_pos);

                imgui.Spacing();
                imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "=== LIVE CURSOR POSITION ===");
                imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "X: %ld  |  Y: %ld", mouse_pos.x, mouse_pos.y);

                HWND hwnd = g_last_swapchain_hwnd.load();
                if (hwnd && IsWindow(hwnd)) {
                    POINT client_pos = mouse_pos;
                    ScreenToClient(hwnd, &client_pos);
                    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Game Window: X: %ld  |  Y: %ld", client_pos.x,
                                      client_pos.y);
                }

                imgui.Spacing();
                if (imgui.Button("Copy Screen Coords")) {
                    std::string coords = std::to_string(mouse_pos.x) + ", " + std::to_string(mouse_pos.y);
                    if (OpenClipboard(nullptr)) {
                        EmptyClipboard();
                        HGLOBAL h_clipboard_data = GlobalAlloc(GMEM_DDESHARE, coords.length() + 1);
                        if (h_clipboard_data) {
                            char* pch_data = static_cast<char*>(GlobalLock(h_clipboard_data));
                            if (pch_data) {
                                strcpy_s(pch_data, coords.length() + 1, coords.c_str());
                                GlobalUnlock(h_clipboard_data);
                                SetClipboardData(CF_TEXT, h_clipboard_data);
                            }
                        }
                        CloseClipboard();
                        LogInfo("Screen coordinates copied to clipboard: %s", coords.c_str());
                    }
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Copy current screen coordinates to clipboard.");
                }

                if (hwnd && IsWindow(hwnd)) {
                    imgui.SameLine();
                    if (imgui.Button("Copy Game Window Coords")) {
                        POINT client_pos = mouse_pos;
                        ScreenToClient(hwnd, &client_pos);
                        std::string coords = std::to_string(client_pos.x) + ", " + std::to_string(client_pos.y);
                        if (OpenClipboard(nullptr)) {
                            EmptyClipboard();
                            HGLOBAL h_clipboard_data = GlobalAlloc(GMEM_DDESHARE, coords.length() + 1);
                            if (h_clipboard_data) {
                                char* pch_data = static_cast<char*>(GlobalLock(h_clipboard_data));
                                if (pch_data) {
                                    strcpy_s(pch_data, coords.length() + 1, coords.c_str());
                                    GlobalUnlock(h_clipboard_data);
                                    SetClipboardData(CF_TEXT, h_clipboard_data);
                                }
                            }
                            CloseClipboard();
                            LogInfo("Game window coordinates copied to clipboard: %s", coords.c_str());
                        }
                    }
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("Copy current game window coordinates to clipboard.");
                    }
                }

                autoclick::DrawAutoClickFeature(imgui);
                imgui.Separator();
                DrawMouseCoordinatesDisplay(imgui);
            }
            imgui.Spacing();
        }

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("Sleep Hook Controls",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawSleepHookControls(imgui);
            }
            imgui.Spacing();
        }

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("Rand Hook Controls",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawRandHookControls(imgui);
            }
            imgui.Spacing();
        }

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("Time Slowdown Controls",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawTimeSlowdownControls(imgui);
            }
            imgui.Spacing();
        }

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("HID Suppression", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawHIDSuppression(imgui);
            }
            imgui.Spacing();

            if (imgui.CollapsingHeader("DualSense Controller Monitor",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                display_commander::widgets::dualsense_widget::DrawDualSenseWidget(imgui);
            }
            imgui.Spacing();
        }

        if (imgui.CollapsingHeader("Developer Tools", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawDeveloperTools(imgui);
        }
        imgui.Spacing();

        if (imgui.CollapsingHeader("Debug Output Hooks", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawDebugOutputHooks(imgui);
        }
        imgui.Spacing();

        if (imgui.CollapsingHeader("DLSS Indicator Controls",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawDlssIndicatorControls(imgui);
        }
        imgui.Spacing();

        if (imgui.CollapsingHeader("Anisotropic Filtering Upgrade",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawAnisotropicFilteringUpgrade(imgui);
        }
        imgui.Spacing();

        if (imgui.CollapsingHeader("DLL Blocking", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawDLLBlockingControls(imgui);
        }

        imgui.EndTabItem();
    }

    if (!is_standalone) {
        if (imgui.BeginTabItem("Window Info", nullptr, 0)) {
            DrawWindowInfoTab(imgui);
            imgui.EndTabItem();
        }

        if (imgui.BeginTabItem("Swapchain", nullptr, 0)) {
            DrawSwapchainTab(imgui, runtime);
            imgui.EndTabItem();
        }

        if (GetModuleHandleW(L"sl.interposer.dll") != nullptr) {
            if (imgui.BeginTabItem("Streamline", nullptr, 0)) {
                DrawStreamlineTab(imgui);
                imgui.EndTabItem();
            }
        }

        if (imgui.BeginTabItem("Hook Statistics", nullptr, 0)) {
            DrawHookStatsTab(imgui);
            imgui.EndTabItem();
        }

        if (imgui.BeginTabItem("Updates", nullptr, 0)) {
            DrawUpdatesTab(imgui);
            imgui.EndTabItem();
        }
    }

    if (imgui.BeginTabItem("Hooks", nullptr, 0)) {
        DrawHooksConfigTab(imgui);
        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Monitor Settings", nullptr, 0)) {
        ui::monitor_settings::DrawMonitorSettings(imgui);
        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Important Info", nullptr, 0)) {
        DrawImportantInfo(imgui);
        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Input", nullptr, 0)) {
        DrawInputTestTab(imgui);
        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Thread Tracking", nullptr, 0)) {
        DrawThreadTrackingSubTab(imgui);
        imgui.EndTabItem();
    }

    imgui.EndTabBar();
}

static void DrawHooksConfigTab(display_commander::ui::IImGuiWrapper& imgui) {
    using namespace display_commander::ui::wrapper_flags;
    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "=== All Hooks (Suppression & Installed) ===");
    imgui.Text(
        "Suppressed: checkbox = hook suppressed (saved to [DisplayCommander.HookSuppression]). Installed = "
        "[DisplayCommander.HooksInstalled]. Changes take effect on next hook install (e.g. game restart).");
    imgui.Separator();
    const int table_flags = TableFlags_Borders | TableFlags_RowBg | TableFlags_Resizable;
    if (imgui.BeginTable("HooksConfigTable", 3, table_flags)) {
        imgui.TableSetupColumn("Hook", TableColumnFlags_WidthFixed, 220.0f);
        imgui.TableSetupColumn("Suppressed", TableColumnFlags_WidthFixed, 100.0f);
        imgui.TableSetupColumn("Installed", TableColumnFlags_WidthFixed, 100.0f);
        imgui.TableHeadersRow();
        auto& mgr = display_commanderhooks::HookSuppressionManager::GetInstance();
        for (int i = 0; i < display_commanderhooks::HookSuppressionManager::kHookTypeCount; ++i) {
            display_commanderhooks::HookType t = display_commanderhooks::HookSuppressionManager::GetHookTypeByIndex(i);
            imgui.TableNextRow();
            imgui.TableSetColumnIndex(0);
            imgui.TextUnformatted(mgr.GetHookTypeName(t).c_str());
            imgui.TableSetColumnIndex(1);
            bool suppressed = mgr.ShouldSuppressHook(t);
            imgui.PushID(i);
            if (imgui.Checkbox("##suppress", &suppressed)) {
                mgr.SetSuppressHook(t, suppressed);
            }
            imgui.PopID();
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Checked = hook suppressed (not installed). Uncheck to allow hook on next load.");
            }
            imgui.TableSetColumnIndex(2);
            bool installed = mgr.IsHookInstalled(t);
            imgui.TextColored(installed ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ui::colors::TEXT_DIMMED,
                              installed ? "Yes" : "No");
        }
        imgui.EndTable();
    }
}

void DrawMouseCoordinatesDisplay(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Current Cursor Position ===");

    POINT mouse_pos;
    GetCursorPos(&mouse_pos);

    imgui.Spacing();
    imgui.TextColored(ImVec4(1.0f, 1.0f, 0.8f, 1.0f), "Current Cursor Position:");
    imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Screen: (%ld, %ld)", mouse_pos.x, mouse_pos.y);

    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd && IsWindow(hwnd)) {
        POINT client_pos = mouse_pos;
        ScreenToClient(hwnd, &client_pos);

        imgui.TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "Game Window: (%ld, %ld)", client_pos.x, client_pos.y);

        RECT window_rect;
        if (GetWindowRect(hwnd, &window_rect)) {
            imgui.Text("Game Window Screen Position: (%ld, %ld) to (%ld, %ld)", window_rect.left, window_rect.top,
                       window_rect.right, window_rect.bottom);
            imgui.Text("Game Window Size: %ld x %ld", window_rect.right - window_rect.left,
                       window_rect.bottom - window_rect.top);
        }

        bool mouse_over_window = (mouse_pos.x >= window_rect.left && mouse_pos.x <= window_rect.right
                                  && mouse_pos.y >= window_rect.top && mouse_pos.y <= window_rect.bottom);

        if (mouse_over_window) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Mouse is over game window");
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " Mouse is outside game window");
        }

    } else {
        imgui.TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ICON_FK_WARNING " No valid game window handle available");
    }

    if (imgui.Button("Refresh Coordinates")) {
        LogInfo("Mouse coordinates refreshed");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Refresh the mouse coordinate display (coordinates update automatically).");
    }

    imgui.Spacing();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Debug Information:");
    imgui.Text("Game Window Handle: 0x%p", hwnd);
    imgui.Text("Window Valid: %s", (hwnd && IsWindow(hwnd)) ? "Yes" : "No");

    HWND foreground_hwnd = display_commanderhooks::GetForegroundWindow_Direct();
    imgui.Text("Foreground Window: 0x%p", foreground_hwnd);
    imgui.Text("Game Window is Foreground: %s", (hwnd == foreground_hwnd) ? "Yes" : "No");
}

// Cleanup function to stop background threads
void CleanupExperimentalTab() {
    // Disable auto-click (thread will sleep when disabled)
    if (g_auto_click_enabled.load()) {
        g_auto_click_enabled.store(false);
        LogInfo("Experimental tab cleanup: Auto-click disabled (thread will sleep)");
    }
}

namespace {
const char* LatencyMarkerTypeName(int index) {
    static const char* names[] = {"SIMULATION_START", "SIMULATION_END", "RENDERSUBMIT_START",
                                  "RENDERSUBMIT_END", "PRESENT_START",  "PRESENT_END"};
    if (index >= 0 && index < static_cast<int>(sizeof(names) / sizeof(names[0]))) {
        return names[index];
    }
    return "?";
}
}  // namespace

static void DrawThreadTrackingSubTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Thread Tracking - Frame Pacing Debug");
    imgui.Separator();
    imgui.Spacing();

    CheckboxSetting(settings::g_experimentalTabSettings.thread_tracking_enabled, "Enable thread tracking", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, records which thread called NvAPI_D3D_SetLatencyMarker (first 6 marker types) and "
            "ChooseFpsLimiter (each call site). Use to debug frame pacing when the game uses another thread for "
            "rendering. Default off to avoid extra overhead.");
    }
    g_thread_tracking_enabled.store(settings::g_experimentalTabSettings.thread_tracking_enabled.GetValue(),
                                    std::memory_order_relaxed);

    imgui.Spacing();
    if (!g_thread_tracking_enabled.load(std::memory_order_relaxed)) {
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Enable thread tracking to see data below.");
        return;
    }

    if (imgui.CollapsingHeader("NvAPI_D3D_SetLatencyMarker_Detour (first 6 marker types)",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Indent();
        imgui.Text("Last thread ID and frame_id reported for each marker type (0 = not yet called):");
        imgui.Spacing();
        for (size_t i = 0; i < kLatencyMarkerTypeCountFirstSix; i++) {
            DWORD tid = g_latency_marker_thread_id[i].load(std::memory_order_relaxed);
            uint64_t frame_id = g_latency_marker_last_frame_id[i].load(std::memory_order_relaxed);
            imgui.Text("%s: TID %lu (0x%lX), frame_id %llu", LatencyMarkerTypeName(static_cast<int>(i)),
                       static_cast<unsigned long>(tid), static_cast<unsigned long>(tid),
                       static_cast<unsigned long long>(frame_id));
        }
        imgui.Unindent();
    }

    imgui.Spacing();
    if (imgui.CollapsingHeader("ChooseFpsLimiter call sites",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Indent();
        imgui.Text("Last thread ID that called ChooseFpsLimiter for each option (0 = not yet called):");
        imgui.Spacing();
        for (size_t i = 0; i < kFpsLimiterCallSiteCount; i++) {
            DWORD tid = g_fps_limiter_site_thread_id[i].load(std::memory_order_relaxed);
            const char* name = FpsLimiterSiteName(static_cast<FpsLimiterCallSite>(i));
            imgui.Text("%s: %lu (0x%lX)", name, static_cast<unsigned long>(tid), static_cast<unsigned long>(tid));
        }
        imgui.Unindent();
    }
}

void DrawBackbufferFormatOverride(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Backbuffer Format Override ===");

    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - May cause compatibility issues!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature overrides the backbuffer format during swapchain creation.\nUse with caution "
            "as it may cause rendering issues or crashes in some games.");
    }

    imgui.Spacing();

    if (CheckboxSetting(settings::g_experimentalTabSettings.backbuffer_format_override_enabled,
                        "Enable Backbuffer Format Override", imgui)) {
        LogInfo(
            "Backbuffer format override %s",
            settings::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Override the backbuffer format during swapchain creation.\nRequires restart to take effect.");
    }

    if (settings::g_experimentalTabSettings.backbuffer_format_override_enabled.GetValue()) {
        imgui.Spacing();
        imgui.Text("Target Format:");

        if (ComboSettingWrapper(settings::g_experimentalTabSettings.backbuffer_format_override, "Format", imgui)) {
            LogInfo("Backbuffer format override changed to: %s",
                    settings::g_experimentalTabSettings.backbuffer_format_override
                        .GetLabels()[settings::g_experimentalTabSettings.backbuffer_format_override.GetValue()]);
        }

        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Select the target backbuffer format:\n"
                "• R8G8B8A8_UNORM: Standard 8-bit per channel (32-bit total)\n"
                "• R10G10B10A2_UNORM: 10-bit RGB + 2-bit alpha (32-bit total)\n"
                "• R16G16B16A16_FLOAT: 16-bit HDR floating point (64-bit total)");
        }

        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");
    }
}

void DrawBufferResolutionUpgrade(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Buffer Resolution Upgrade ===");

    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - May cause performance issues!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature upgrades internal buffer resolutions during resource creation.\nUse with "
            "caution as it may cause performance issues or rendering artifacts.");
    }

    imgui.Spacing();

    if (CheckboxSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled,
                        "Enable Buffer Resolution Upgrade", imgui)) {
        LogInfo(
            "Buffer resolution upgrade %s",
            settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Upgrade internal buffer resolutions during resource creation.\nRequires restart to take effect.");
    }

    if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_enabled.GetValue()) {
        imgui.Spacing();

        if (ComboSettingWrapper(settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode, "Upgrade Mode",
                                imgui)) {
            LogInfo("Buffer resolution upgrade mode changed to: %s",
                    settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode
                        .GetLabels()[settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue()]);
        }

        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Select the buffer resolution upgrade mode:\n"
                "• Upgrade 1280x720 by Scale Factor: Specifically upgrade 1280x720 buffers by the scale factor\n"
                "• Upgrade by Scale Factor: Scale all buffers by the specified factor\n"
                "• Upgrade Custom Resolution: Upgrade specific resolution to custom target");
        }

        if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 0
            || settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 1) {
            imgui.Spacing();
            imgui.Text("Scale Factor:");

            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor,
                                 "Scale Factor", "%d", imgui)) {
                LogInfo("Buffer resolution upgrade scale factor changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue());
            }

            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Scale factor to apply to all buffer resolutions (1-4x)");
            }
        }

        if (settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue() == 2) {
            imgui.Spacing();
            imgui.Text("Target Resolution:");

            imgui.SetNextItemWidth(120);
            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_width, "Width", "%d",
                                 imgui)) {
                LogInfo("Buffer resolution upgrade width changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue());
            }

            imgui.SameLine();
            imgui.SetNextItemWidth(120);
            if (SliderIntSetting(settings::g_experimentalTabSettings.buffer_resolution_upgrade_height, "Height", "%d",
                                 imgui)) {
                LogInfo("Buffer resolution upgrade height changed to: %d",
                        settings::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue());
            }

            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Target resolution for buffer upgrades.\nWidth: 320-7680, Height: 240-4320");
            }
        }

        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");

        int mode = settings::g_experimentalTabSettings.buffer_resolution_upgrade_mode.GetValue();
        int scale = settings::g_experimentalTabSettings.buffer_resolution_upgrade_scale_factor.GetValue();
        if (mode == 0) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will upgrade 1280x720 buffers to %dx%d (%dx scale)",
                              1280 * scale, 720 * scale, scale);
        } else if (mode == 1) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will scale all buffers by %dx", scale);
        } else if (mode == 2) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "Will upgrade buffers to: %dx%d",
                              settings::g_experimentalTabSettings.buffer_resolution_upgrade_width.GetValue(),
                              settings::g_experimentalTabSettings.buffer_resolution_upgrade_height.GetValue());
        }
    }
}

void DrawTextureFormatUpgrade(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Texture Format Upgrade ===");

    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - May cause performance issues!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature upgrades texture formats to RGB16A16 during resource creation.\nUse with "
            "caution as it may cause performance issues or rendering artifacts.");
    }

    imgui.Spacing();

    if (CheckboxSetting(settings::g_experimentalTabSettings.texture_format_upgrade_enabled,
                        "Upgrade Textures to RGB16A16", imgui)) {
        LogInfo("Texture format upgrade %s",
                settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue() ? "enabled" : "disabled");
    }

    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Upgrade texture formats to RGB16A16 (16-bit per channel) for textures at 720p, 1440p, and "
            "4K resolutions.\nRequires restart to take effect.");
    }

    imgui.Spacing();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Changes require restart to take effect");

    if (settings::g_experimentalTabSettings.texture_format_upgrade_enabled.GetValue()) {
        imgui.TextColored(
            ImVec4(0.8f, 1.0f, 0.8f, 1.0f),
            "Will upgrade texture formats to RGB16A16 (16-bit per channel) for 720p, 1440p, and 4K textures");
    }
}

void DrawSleepHookControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Sleep Hook Controls ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Hooks game sleep calls for FPS control!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature hooks Windows Sleep APIs (Sleep, SleepEx, WaitForSingleObject, WaitForMultipleObjects) to "
            "modify sleep durations.\nUseful for games that use sleep-based FPS limiting like Unity games.");
    }

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.sleep_hook_enabled, "Enable Sleep Hooks", imgui)) {
        LogInfo("Sleep hooks %s",
                settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable hooks for Windows Sleep APIs to modify sleep durations for FPS control.");
    }

    // Render thread only option removed

    if (settings::g_experimentalTabSettings.sleep_hook_enabled.GetValue()) {
        imgui.Spacing();

        // Sleep multiplier slider
        if (SliderFloatSetting(settings::g_experimentalTabSettings.sleep_multiplier, "Sleep Multiplier", "%.2fx",
                               imgui)) {
            LogInfo("Sleep multiplier set to %.2fx", settings::g_experimentalTabSettings.sleep_multiplier.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Multiplier applied to sleep durations. 1.0 = no change, 0.5 = half duration, 2.0 = double duration.");
        }

        // Min sleep duration slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.min_sleep_duration_ms, "Min Sleep Duration (ms)",
                             "%d ms", imgui)) {
            LogInfo("Min sleep duration set to %d ms",
                    settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Minimum sleep duration in milliseconds. 0 = no minimum limit.");
        }

        // Max sleep duration slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.max_sleep_duration_ms, "Max Sleep Duration (ms)",
                             "%d ms", imgui)) {
            LogInfo("Max sleep duration set to %d ms",
                    settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Maximum sleep duration in milliseconds. 0 = no maximum limit.");
        }

        imgui.Spacing();

        // Show current settings summary
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Multiplier: %.2fx",
                          settings::g_experimentalTabSettings.sleep_multiplier.GetValue());
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Min Duration: %d ms",
                          settings::g_experimentalTabSettings.min_sleep_duration_ms.GetValue());
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Max Duration: %d ms",
                          settings::g_experimentalTabSettings.max_sleep_duration_ms.GetValue());

        // Show hook statistics if available
        if (display_commanderhooks::g_sleep_hook_stats.total_calls.load() > 0) {
            imgui.Spacing();
            imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Hook Statistics:");
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Calls: %llu",
                              display_commanderhooks::g_sleep_hook_stats.total_calls.load());
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Modified Calls: %llu",
                              display_commanderhooks::g_sleep_hook_stats.modified_calls.load());

            uint64_t total_original = display_commanderhooks::g_sleep_hook_stats.total_original_duration_ms.load();
            uint64_t total_modified = display_commanderhooks::g_sleep_hook_stats.total_modified_duration_ms.load();
            if (total_original > 0) {
                imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Original Duration: %llu ms", total_original);
                imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total Modified Duration: %llu ms", total_modified);
                imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Time Saved: %lld ms",
                                  static_cast<int64_t>(total_original) - static_cast<int64_t>(total_modified));
            }
        }
    }
}

void DrawRandHookControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Rand Hook Controls ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FK_WARNING
                      " EXPERIMENTAL FEATURE - Hooks C runtime rand() function to return constant value!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature hooks the C runtime rand() function from msvcrt.dll or ucrtbase.dll.\n"
            "When enabled, rand() will always return the configured constant value instead of random numbers.\n"
            "Useful for games that use rand() for randomization that you want to control.");
    }

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.rand_hook_enabled, "Enable Rand Hook", imgui)) {
        LogInfo("Rand hook %s",
                settings::g_experimentalTabSettings.rand_hook_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable hook for C runtime rand() function to return a constant value.");
    }

    if (settings::g_experimentalTabSettings.rand_hook_enabled.GetValue()) {
        imgui.Spacing();

        // Rand value slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.rand_hook_value, "Rand Value", "%d", imgui)) {
            LogInfo("Rand hook value set to %d", settings::g_experimentalTabSettings.rand_hook_value.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Constant value that rand() will return when the hook is enabled.\n"
                "Range: %d (INT_MIN) to %d (INT_MAX)\n"
                "Note: Standard rand() returns 0 to %d (RAND_MAX), but the hook allows any int value including "
                "negatives.",
                INT_MIN, INT_MAX, RAND_MAX);
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Statistics
        uint64_t rand_calls = display_commanderhooks::GetRandCallCount();
        bool hooks_installed = display_commanderhooks::AreRandHooksInstalled();

        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Statistics:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total rand() calls: %llu", rand_calls);
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Hooks Status: %s",
                          hooks_installed ? "Installed" : "Not Installed");

        // Show current settings
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Rand Value: %d",
                          settings::g_experimentalTabSettings.rand_hook_value.GetValue());

        imgui.Spacing();
        imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          ICON_FK_WARNING " WARNING: This affects all code that uses rand()!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "The rand() hook affects all code in the game process that calls rand(),\n"
                "including game logic, AI, procedural generation, etc.");
        }
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Rand_s hook controls
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Rand_s Hook Controls ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FK_WARNING
                      " EXPERIMENTAL FEATURE - Hooks C runtime rand_s() function to return constant value!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature hooks the C runtime rand_s() function from msvcrt.dll or ucrtbase.dll.\n"
            "rand_s() is the secure version of rand() that uses cryptographically secure random number generation.\n"
            "When enabled, rand_s() will always return the configured constant value instead of random numbers.\n"
            "Useful for games that use rand_s() for randomization that you want to control.");
    }

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.rand_s_hook_enabled, "Enable Rand_s Hook", imgui)) {
        LogInfo("Rand_s hook %s",
                settings::g_experimentalTabSettings.rand_s_hook_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable hook for C runtime rand_s() function to return a constant value.");
    }

    if (settings::g_experimentalTabSettings.rand_s_hook_enabled.GetValue()) {
        imgui.Spacing();

        // Rand_s value slider
        if (SliderIntSetting(settings::g_experimentalTabSettings.rand_s_hook_value, "Rand_s Value", "%u", imgui)) {
            LogInfo("Rand_s hook value set to %u", settings::g_experimentalTabSettings.rand_s_hook_value.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Constant value that rand_s() will return when the hook is enabled.\n"
                "Range: 0 to %u (UINT_MAX)",
                UINT_MAX);
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Statistics
        uint64_t rand_s_calls = display_commanderhooks::GetRand_sCallCount();
        bool hooks_installed = display_commanderhooks::AreRandHooksInstalled();

        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Statistics:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Total rand_s() calls: %llu", rand_s_calls);
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Hooks Status: %s",
                          hooks_installed ? "Installed" : "Not Installed");

        // Show current settings
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Rand_s Value: %u",
                          settings::g_experimentalTabSettings.rand_s_hook_value.GetValue());

        imgui.Spacing();
        imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          ICON_FK_WARNING " WARNING: This affects all code that uses rand_s()!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "The rand_s() hook affects all code in the game process that calls rand_s(),\n"
                "including game logic, AI, procedural generation, etc.\n"
                "Note: rand_s() is designed for cryptographically secure random numbers,\n"
                "so hooking it may affect security-sensitive operations.");
        }
    }
}

void DrawTimeSlowdownControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Time Slowdown Controls ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Manipulates game time via multiple timer APIs!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature hooks multiple timer APIs to manipulate game time.\nUseful for bypassing FPS "
            "limits and slowing down/speeding up games that use various timing methods.");
    }

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.timeslowdown_enabled, "Enable Time Slowdown", imgui)) {
        LogInfo("Time slowdown %s",
                settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable time manipulation via timer API hooks.");
    }
    imgui.SameLine();

    // Compatibility mode checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.timeslowdown_compatibility_mode, "Compatibility Mode",
                        imgui)) {
        LogInfo(
            "Time slowdown compatibility mode %s",
            settings::g_experimentalTabSettings.timeslowdown_compatibility_mode.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Enable compatibility mode for time slowdown hooks. This may improve compatibility with certain games.");
    }
    imgui.SameLine();
    if (imgui.SmallButton("Reset TS")) {
        // Reset time slowdown to defaults
        settings::g_experimentalTabSettings.timeslowdown_enabled.SetValue(false);
        display_commanderhooks::SetTimeslowdownEnabled(false);
        settings::g_experimentalTabSettings.timeslowdown_multiplier.SetValue(1.0f);
        display_commanderhooks::SetTimeslowdownMultiplier(1.0f);

        LogInfo("Time slowdown reset: disabled and multiplier set to 1.0x");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Disable Time Slowdown and set multiplier to 1.0x.");
    }

    if (settings::g_experimentalTabSettings.timeslowdown_enabled.GetValue()) {
        imgui.Spacing();

        // Max time multiplier slider (controls upper bound of Time Multiplier)
        if (SliderFloatSetting(settings::g_experimentalTabSettings.timeslowdown_max_multiplier, "Max Time Multiplier",
                               "%.0fx", imgui)) {
            float new_max = settings::g_experimentalTabSettings.timeslowdown_max_multiplier.GetValue();
            settings::g_experimentalTabSettings.timeslowdown_multiplier.SetMax(new_max);
            float cur = settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue();
            if (cur > new_max) {
                settings::g_experimentalTabSettings.timeslowdown_multiplier.SetValue(new_max);
            }
            LogInfo("Max time multiplier set to %.0fx", new_max);
        } else {
            // Ensure the slider respects the current max even if unchanged this frame
            settings::g_experimentalTabSettings.timeslowdown_multiplier.SetMax(
                settings::g_experimentalTabSettings.timeslowdown_max_multiplier.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Sets the maximum allowed value for Time Multiplier (1–1000x).");
        }

        // Time multiplier slider
        if (SliderFloatSetting(settings::g_experimentalTabSettings.timeslowdown_multiplier, "Time Multiplier", "%.2fx",
                               imgui)) {
            LogInfo("Time multiplier set to %.2fx",
                    settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Multiplier for game time. 1.0 = normal speed, 0.5 = half speed, 2.0 = double speed.");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Timer Hook Selection
        imgui.TextColored(ImVec4(0.9f, 0.9f, 1.0f, 1.0f), "Timer Hook Selection:");
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                          "Choose which timer APIs to hook (None/Enabled/Render Thread/Non-Render Thread)");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Select which timer APIs to hook for time manipulation.\n\nOptions:\n- None: Disabled\n- Enabled: Hook "
                "all threads\n- Enable Render Thread: Only hook the render thread (detected from swapchain "
                "creation)\n- Enable Non-Render Thread: Hook all threads except the render thread");
        }

        imgui.Spacing();

        // QueryPerformanceCounter hook
        uint64_t qpc_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::QueryPerformanceCounter);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.query_performance_counter_hook,
                                "QueryPerformanceCounter", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.query_performance_counter_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(
                display_commanderhooks::TimerHookIdentifier::QueryPerformanceCounter, type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", qpc_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "High-resolution timer used by most modern games for precise timing.\n\nThread-specific modes (Render "
                "Thread/Non-Render Thread) require swapchain initialization to detect the render thread.");
        }

        // Display QPC calling modules with enable/disable checkboxes
        {
            static std::vector<std::pair<HMODULE, std::wstring>> cached_modules;
            static uint64_t last_update_frame = 0;
            uint64_t current_frame = imgui.GetFrameCount();

            // Update module list every 60 frames (~1 second at 60 FPS)
            if (current_frame - last_update_frame > 60 || cached_modules.empty()) {
                cached_modules = display_commanderhooks::GetQPCallingModulesWithHandles();
                last_update_frame = current_frame;
            }

            if (!cached_modules.empty()) {
                imgui.Indent();
                imgui.TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Calling Modules (%zu):", cached_modules.size());
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "DLLs/modules that have called QueryPerformanceCounter\n\nCheck/uncheck to enable/disable time "
                        "slowdown for specific modules");
                }

                // Show modules in a scrollable child window if there are many
                if (cached_modules.size() > 5) {
                    if (imgui.BeginChild("QPCModules", ImVec2(0, 200), true)) {
                        for (const auto& module_pair : cached_modules) {
                            HMODULE hModule = module_pair.first;
                            const std::wstring& module_name = module_pair.second;

                            bool enabled = display_commanderhooks::IsQPCModuleEnabled(hModule);
                            if (imgui.Checkbox(
                                    ("##QPCModule_" + std::to_string(reinterpret_cast<uintptr_t>(hModule))).c_str(),
                                    &enabled)) {
                                display_commanderhooks::SetQPCModuleEnabled(hModule, enabled);
                                // Save enabled modules to settings
                                std::string enabled_modules_str =
                                    display_commanderhooks::SaveQPCEnabledModulesToSettings();
                                settings::g_experimentalTabSettings.qpc_enabled_modules.SetValue(enabled_modules_str);
                                settings::g_experimentalTabSettings.qpc_enabled_modules.Save();
                                LogInfo("QPC module %ls %s", module_name.c_str(), enabled ? "enabled" : "disabled");
                            }
                            imgui.SameLine();
                            imgui.Text("%ls", module_name.c_str());
                        }
                    }
                    imgui.EndChild();
                } else {
                    for (const auto& module_pair : cached_modules) {
                        HMODULE hModule = module_pair.first;
                        const std::wstring& module_name = module_pair.second;

                        bool enabled = display_commanderhooks::IsQPCModuleEnabled(hModule);
                        if (imgui.Checkbox(
                                ("##QPCModule_" + std::to_string(reinterpret_cast<uintptr_t>(hModule))).c_str(),
                                &enabled)) {
                            display_commanderhooks::SetQPCModuleEnabled(hModule, enabled);
                            // Save enabled modules to settings
                            std::string enabled_modules_str = display_commanderhooks::SaveQPCEnabledModulesToSettings();
                            settings::g_experimentalTabSettings.qpc_enabled_modules.SetValue(enabled_modules_str);
                            settings::g_experimentalTabSettings.qpc_enabled_modules.Save();
                            LogInfo("QPC module %ls %s", module_name.c_str(), enabled ? "enabled" : "disabled");
                        }
                        imgui.SameLine();
                        imgui.Text("%ls", module_name.c_str());
                    }
                }

                imgui.Spacing();
                if (imgui.SmallButton("Save##QPCModules")) {
                    std::string enabled_modules_str = display_commanderhooks::SaveQPCEnabledModulesToSettings();
                    settings::g_experimentalTabSettings.qpc_enabled_modules.SetValue(enabled_modules_str);
                    settings::g_experimentalTabSettings.qpc_enabled_modules.Save();
                    LogInfo("QPC enabled modules saved: %s",
                            enabled_modules_str.empty() ? "(none)" : enabled_modules_str.c_str());
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Save the current enabled/disabled state of all modules to settings.\nThis list will be "
                        "automatically loaded on next startup.");
                }
                imgui.SameLine();
                if (imgui.SmallButton("Select All##QPCModules")) {
                    for (const auto& module_pair : cached_modules) {
                        display_commanderhooks::SetQPCModuleEnabled(module_pair.first, true);
                    }
                    LogInfo("All QPC modules enabled (%zu modules)", cached_modules.size());
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Enable time slowdown for all tracked modules");
                }
                imgui.SameLine();
                if (imgui.SmallButton("Clear##QPCModules")) {
                    display_commanderhooks::ClearQPCallingModules();
                    cached_modules.clear();
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Clear the list of tracked calling modules");
                }

                imgui.Unindent();
            }
        }

        // GetTickCount hook
        uint64_t gtc_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetTickCount);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_tick_count_hook, "GetTickCount", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_tick_count_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::GetTickCount,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", gtc_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("32-bit millisecond timer, commonly used by older games.");
        }

        // GetTickCount64 hook
        uint64_t gtc64_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetTickCount64);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_tick_count64_hook, "GetTickCount64", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_tick_count64_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::GetTickCount64,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", gtc64_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("64-bit millisecond timer, used by some modern games.");
        }

        // timeGetTime hook
        uint64_t tgt_calls =
            display_commanderhooks::GetTimerHookCallCountById(display_commanderhooks::TimerHookIdentifier::TimeGetTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.time_get_time_hook, "timeGetTime", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.time_get_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::TimeGetTime,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", tgt_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Multimedia timer, often used for audio/video timing.");
        }

        // GetSystemTime hook
        uint64_t gst_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetSystemTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_hook, "GetSystemTime", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_system_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::GetSystemTime,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", gst_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("System time in SYSTEMTIME format, used by some games for timestamps.");
        }

        // GetSystemTimeAsFileTime hook
        uint64_t gst_aft_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetSystemTimeAsFileTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_as_file_time_hook,
                                "GetSystemTimeAsFileTime", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_system_time_as_file_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(
                display_commanderhooks::TimerHookIdentifier::GetSystemTimeAsFileTime, type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", gst_aft_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("System time in FILETIME format, used by some games for high-precision timestamps.");
        }

        // GetSystemTimePreciseAsFileTime hook
        uint64_t gstp_aft_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetSystemTimePreciseAsFileTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook,
                                "GetSystemTimePreciseAsFileTime", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_system_time_precise_as_file_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(
                display_commanderhooks::TimerHookIdentifier::GetSystemTimePreciseAsFileTime, type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", gstp_aft_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("High-precision system time (Windows 8+), used by modern games for precise timing.");
        }

        // GetLocalTime hook
        uint64_t glt_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::GetLocalTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.get_local_time_hook, "GetLocalTime", imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.get_local_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::GetLocalTime,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", glt_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Local system time (vs UTC), used by some games for timezone-aware timing.");
        }

        // NtQuerySystemTime hook
        uint64_t ntqst_calls = display_commanderhooks::GetTimerHookCallCountById(
            display_commanderhooks::TimerHookIdentifier::NtQuerySystemTime);
        if (ComboSettingWrapper(settings::g_experimentalTabSettings.nt_query_system_time_hook, "NtQuerySystemTime",
                                imgui)) {
            display_commanderhooks::TimerHookType type = static_cast<display_commanderhooks::TimerHookType>(
                settings::g_experimentalTabSettings.nt_query_system_time_hook.GetValue());
            display_commanderhooks::SetTimerHookTypeById(display_commanderhooks::TimerHookIdentifier::NtQuerySystemTime,
                                                         type);
        }
        imgui.SameLine();
        imgui.Text("[%llu calls]", ntqst_calls);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Native API system time, used by some games for low-level timing access.");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Show current settings summary
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Time Multiplier: %.2fx",
                          settings::g_experimentalTabSettings.timeslowdown_multiplier.GetValue());
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Max Time Multiplier: %.0fx",
                          settings::g_experimentalTabSettings.timeslowdown_max_multiplier.GetValue());

        // QPC comparison display
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "QPC Comparison:");

        if (display_commanderhooks::QueryPerformanceCounter_Original
            && display_commanderhooks::QueryPerformanceFrequency_Original) {
            // Get QPC frequency
            LARGE_INTEGER frequency;
            if (display_commanderhooks::QueryPerformanceFrequency_Original(&frequency) && frequency.QuadPart > 0) {
                // Get original QPC value
                LARGE_INTEGER original_qpc;
                if (display_commanderhooks::QueryPerformanceCounter_Original(&original_qpc)) {
                    // Apply timeslowdown to get spoofed QPC value
                    LONGLONG spoofed_qpc = display_commanderhooks::ApplyTimeslowdownToQPC(original_qpc.QuadPart);

                    // Convert QPC ticks to seconds
                    double original_qpc_seconds =
                        static_cast<double>(original_qpc.QuadPart) / static_cast<double>(frequency.QuadPart);
                    double spoofed_qpc_seconds =
                        static_cast<double>(spoofed_qpc) / static_cast<double>(frequency.QuadPart);
                    double qpc_difference_seconds = spoofed_qpc_seconds - original_qpc_seconds;

                    // Display the comparison
                    imgui.TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "  Original QPC: %.1f s", original_qpc_seconds);
                    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.6f, 1.0f), "  Spoofed QPC: %.1f s", spoofed_qpc_seconds);

                    // Color code the difference based on magnitude
                    ImVec4 qpc_diff_color;
                    double abs_diff_seconds = abs(qpc_difference_seconds);
                    if (abs_diff_seconds < 0.001) {
                        qpc_diff_color = ImVec4(0.6f, 1.0f, 0.6f, 1.0f);  // Green for minimal difference
                    } else if (abs_diff_seconds < 0.01) {
                        qpc_diff_color = ImVec4(1.0f, 1.0f, 0.6f, 1.0f);  // Yellow for small difference
                    } else {
                        qpc_diff_color = ImVec4(1.0f, 0.6f, 0.6f, 1.0f);  // Red for significant difference
                    }

                    imgui.TextColored(qpc_diff_color, "  Difference: %+.1f s", qpc_difference_seconds);

                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx(
                            "Shows the difference between original QueryPerformanceCounter value and spoofed value.\n"
                            "This directly compares what QueryPerformanceCounter_Original returns vs what "
                            "ApplyTimeslowdownToQPC returns.\n"
                            "Positive values mean the spoofed time is ahead of original time.\n"
                            "Negative values mean the spoofed time is behind original time.");
                    }
                } else {
                    imgui.TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "  Failed to get QPC value");
                }
            } else {
                imgui.TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), "  Failed to get QPC frequency");
            }
        } else {
            imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  QPC hooks not available");
        }

        // Show hook status
        bool hooks_installed = display_commanderhooks::AreTimeslowdownHooksInstalled();
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Hooks Status: %s",
                          hooks_installed ? "Installed" : "Not Installed");

        // Show current runtime values
        double current_multiplier = display_commanderhooks::GetTimeslowdownMultiplier();
        bool current_enabled = display_commanderhooks::IsTimeslowdownEnabled();
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Runtime Multiplier: %.2fx", current_multiplier);
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Runtime Enabled: %s", current_enabled ? "Yes" : "No");

        // Show active hooks
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Active Hooks:");
        const char* hook_names[] = {"QueryPerformanceCounter",
                                    "GetTickCount",
                                    "GetTickCount64",
                                    "timeGetTime",
                                    "GetSystemTime",
                                    "GetSystemTimeAsFileTime",
                                    "GetSystemTimePreciseAsFileTime",
                                    "GetLocalTime",
                                    "NtQuerySystemTime"};
        display_commanderhooks::TimerHookIdentifier hook_identifiers[] = {
            display_commanderhooks::TimerHookIdentifier::QueryPerformanceCounter,
            display_commanderhooks::TimerHookIdentifier::GetTickCount,
            display_commanderhooks::TimerHookIdentifier::GetTickCount64,
            display_commanderhooks::TimerHookIdentifier::TimeGetTime,
            display_commanderhooks::TimerHookIdentifier::GetSystemTime,
            display_commanderhooks::TimerHookIdentifier::GetSystemTimeAsFileTime,
            display_commanderhooks::TimerHookIdentifier::GetSystemTimePreciseAsFileTime,
            display_commanderhooks::TimerHookIdentifier::GetLocalTime,
            display_commanderhooks::TimerHookIdentifier::NtQuerySystemTime};

        for (int i = 0; i < 9; i++) {
            if (display_commanderhooks::IsTimerHookEnabledById(hook_identifiers[i])) {
                imgui.TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "    %s", hook_names[i]);
            }
        }

        imgui.Spacing();
        imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          ICON_FK_WARNING " WARNING: This affects all time-based game logic!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Time slowdown affects all game systems that use the selected timer APIs for timing.");
        }
    }
    if (imgui.CollapsingHeader("DLSS Indicator Controls", display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
        DrawDlssIndicatorControls(imgui);
    }
}

void DrawD3D9FlipExControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Direct3D 9 FLIPEX Upgrade ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Upgrades D3D9 games to use FLIPEX swap effect!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature upgrades Direct3D 9 games to use the D3DSWAPEFFECT_FLIPEX swap effect.\n"
            "FLIPEX leverages the Desktop Window Manager (DWM) for better performance on Windows Vista+.\n"
            "Requirements:\n"
            "  - Direct3D 9Ex support (Windows Vista or later)\n"
            "  - Full-screen mode (not windowed)\n"
            "  - At least 2 back buffers\n"
            "  - Driver support for FLIPEX\n"
            "\n"
            "Benefits:\n"
            "  - Reduced input latency\n"
            "  - Better frame pacing\n"
            "  - Improved performance in full-screen mode\n"
            "\n"
            "Note: Not all games and drivers support FLIPEX. If device creation fails,\n"
            "disable this feature.");
    }

    imgui.Spacing();

    // With ReShade: OnCreateDevice / OnCreateSwapchain path
    if (CheckboxSetting(settings::g_experimentalTabSettings.d3d9_flipex_enabled, "Enable D3D9 FLIPEX (with ReShade)",
                        imgui)) {
        LogInfo("D3D9 FLIPEX (ReShade) %s",
                settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When ReShade is loaded: upgrade D3D9 to D3D9Ex and FLIPEX via OnCreateDevice / swapchain.\n"
            "Requires full-screen and D3D9Ex support.");
    }

    // Without ReShade: CreateDevice/CreateDeviceEx detour path
    if (CheckboxSetting(settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade,
                        "Enable D3D9 FLIPEX (no-ReShade mode)", imgui)) {
        LogInfo("D3D9 FLIPEX (no-ReShade) %s",
                settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When ReShade is not loaded: hook CreateDevice and upgrade to CreateDeviceEx + FLIPEX.\n"
            "Requires restart. Uses D3D9Ex managed pool (6) for resource creation.");
    }

    if (CheckboxSetting(settings::g_experimentalTabSettings.d3d9_fix_create_texture_dimensions,
                        "Fix CreateTexture dimensions (DXT/BC)", imgui)) {
        LogInfo(
            "D3D9 Fix CreateTexture dimensions %s",
            settings::g_experimentalTabSettings.d3d9_fix_create_texture_dimensions.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, CreateTexture/CreateVolumeTexture/CreateCubeTexture width/height/depth are rounded up to a "
            "multiple of 4 for DXT1-DXT5 (and other block-compressed) formats.\n"
            "Fixes D3DERR_INVALIDCALL when games pass invalid dimensions (e.g. width=2 with DXT5).");
    }

    imgui.Spacing();

    // Display current D3D9 state if applicable
    const reshade::api::device_api current_api = g_last_reshade_device_api.load();
    uint32_t api_version = g_last_api_version.load();

    if (current_api == reshade::api::device_api::d3d9) {
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Game API:");
        imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  Direct3D 9");

        if (s_d3d9e_upgrade_successful.load()) {
            api_version = static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex);  // due to reshade's bug.
        }

        if (api_version == static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9Ex)) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  API Version: Direct3D 9Ex (FLIPEX compatible)");
        } else if (api_version == static_cast<uint32_t>(display_commander::D3D9ApiVersion::D3D9)) {
            imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "  API Version: Direct3D 9 (Needs D3D9Ex upgrade)");
        } else {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  API Version: 0x%x", api_version);
        }

        // Display current FlipEx state
        bool using_flipex = g_used_flipex.load();
        if (using_flipex) {
            imgui.TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  Swap Effect: FLIPEX (Fast Flip)");
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "  Swap Effect: Composite (Standard)");
        }
    } else {
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current game is not using Direct3D 9");
    }

    imgui.Spacing();

    // Information
    imgui.TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "How it works:");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "1. Enable the feature above");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "2. Restart the game");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "3. The addon will upgrade D3D9 to D3D9Ex if needed");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "4. The addon will modify swap effect to FLIPEX");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "5. Check the log file for upgrade status");

    imgui.Spacing();
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " WARNING: If the game fails to start, disable this feature!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Some games and drivers don't support FLIPEX.\n"
            "If you experience crashes or black screens, disable this feature.");
    }
}

void DrawDlssIndicatorControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== DLSS Indicator Controls ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Modifies NVIDIA registry settings!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature modifies the NVIDIA registry to enable/disable the DLSS indicator.\n"
            "The indicator appears in the bottom left corner when enabled.\n"
            "Requires administrator privileges to modify registry.");
    }

    imgui.Spacing();

    // Current status display
    bool current_status = dlss::DlssIndicatorManager::IsDlssIndicatorEnabled();
    DWORD current_value = dlss::DlssIndicatorManager::GetDlssIndicatorValue();

    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Status:");
    imgui.TextColored(current_status ? ImVec4(0.0f, 1.0f, 0.0f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                      "  DLSS Indicator: %s", current_status ? "ENABLED" : "DISABLED");
    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Registry Value: %lu (0x%lX)", current_value, current_value);
    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Registry Path: HKEY_LOCAL_MACHINE\\%s",
                      dlss::DlssIndicatorManager::GetRegistryKeyPath().c_str());
    imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Value Name: %s",
                      dlss::DlssIndicatorManager::GetRegistryValueName().c_str());

    imgui.Spacing();

    // Enable/disable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.dlss_indicator_enabled, "Enable DLSS Indicator", imgui)) {
        LogInfo("DLSS Indicator setting %s",
                settings::g_experimentalTabSettings.dlss_indicator_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable DLSS indicator in games. This modifies the NVIDIA registry.");
    }

    imgui.Spacing();

    // Action buttons
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Registry Actions:");

    // Generate Enable .reg file button
    if (imgui.Button("Generate Enable .reg File")) {
        std::string reg_content = dlss::DlssIndicatorManager::GenerateEnableRegFile();
        std::string filename = "dlss_indicator_enable.reg";

        if (dlss::DlssIndicatorManager::WriteRegFile(reg_content, filename)) {
            LogInfo("DLSS Indicator: Enable .reg file generated: %s", filename.c_str());
        } else {
            LogError("DLSS Indicator: Failed to generate enable .reg file");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Generate a .reg file to enable DLSS indicator.\n"
            "The file will be created in the current directory.");
    }

    imgui.SameLine();

    // Generate Disable .reg file button
    if (imgui.Button("Generate Disable .reg File")) {
        std::string reg_content = dlss::DlssIndicatorManager::GenerateDisableRegFile();
        std::string filename = "dlss_indicator_disable.reg";

        if (dlss::DlssIndicatorManager::WriteRegFile(reg_content, filename)) {
            LogInfo("DLSS Indicator: Disable .reg file generated: %s", filename.c_str());
        } else {
            LogError("DLSS Indicator: Failed to generate disable .reg file");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Generate a .reg file to disable DLSS indicator.\n"
            "The file will be created in the current directory.");
    }

    imgui.SameLine();

    // Open folder button
    if (imgui.Button("Open .reg Files Folder")) {
        // Get current working directory
        char current_dir[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, current_dir) != 0) {
            // Use ShellExecute to open the folder in Windows Explorer
            HINSTANCE result = ShellExecuteA(nullptr, "open", current_dir, nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32) {
                LogError("DLSS Indicator: Failed to open folder, error: %ld", reinterpret_cast<INT_PTR>(result));
            } else {
                LogInfo("DLSS Indicator: Opened folder: %s", current_dir);
            }
        } else {
            LogError("DLSS Indicator: Failed to get current directory");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Open the folder containing the generated .reg files in Windows Explorer.");
    }

    imgui.Spacing();

    // Instructions
    imgui.TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Instructions:");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "1. Generate the appropriate .reg file using the buttons above");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                      "2. Open the folder and double-click the .reg file to apply changes");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                      "3. Windows will prompt for administrator privileges when executing");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "4. Restart your game to see the DLSS indicator");
    imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                      "5. The indicator appears in the bottom left corner when enabled");

    imgui.Spacing();
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " WARNING: Registry modifications require administrator privileges!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "The registry modification requires administrator privileges.\n"
            "Windows will prompt for elevation when executing .reg files.");
    }
}

void DrawDeveloperTools(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Developer Tools ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - For debugging purposes only!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "These tools are for developers and debugging purposes.\nUse with caution as they can cause crashes or "
            "unexpected behavior.");
    }

    imgui.Spacing();

    // Link dependencies (libraries linked into the addon DLL)
    if (imgui.TreeNode("Link dependencies (addon DLL)")) {
        imgui.TextDisabled("Libraries linked into Display Commander addon:");
        imgui.Indent();
        for (int i = 0; i < display_commander::res::kAddonLinkLibrariesCount; ++i) {
            const auto& e = display_commander::res::kAddonLinkLibraries[i];
            imgui.BulletText("%s%s%s", e.name, e.note ? " - " : "", e.note ? e.note : "");
        }
        imgui.Unindent();
        imgui.TreePop();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "System import libs load DLLs at runtime (e.g. setupapi.dll). MinHook is static (code merged into addon).");
    }

    imgui.Spacing();

    // Apply changes in create_swapchain event (OnCreateSwapchainCapture2)
    CheckboxSetting(settings::g_experimentalTabSettings.apply_changes_on_create_swapchain,
                    "Apply changes in OnCreateSwapchain (create_swapchain event)", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, OnCreateSwapchainCapture2 applies all modifications (prevent fullscreen, backbuffer "
            "count, FLIPEX, format override, resolution upgrade, etc.). When disabled, only capture of game "
            "resolution is done.");
    }

    imgui.Spacing();

    // Spoof game resolution in WM_SIZE/WM_DISPLAYCHANGE (like SpecialK res override)
    CheckboxSetting(settings::g_experimentalTabSettings.spoof_game_resolution_in_size_messages,
                    "Spoof game resolution in size messages", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, WM_SIZE and WM_DISPLAYCHANGE report the game's render resolution (from swap chain) instead "
            "of the real window size. Can help keep the swap chain from resizing when moving between monitors or "
            "resizing the window (similar to SpecialK's resolution override).");
    }
    if (settings::g_experimentalTabSettings.spoof_game_resolution_in_size_messages.GetValue()) {
        imgui.Indent();
        int override_x = settings::g_experimentalTabSettings.spoof_game_resolution_override_width.GetValue();
        if (imgui.InputInt("Override X", &override_x, 0, 0)) {
            override_x = std::clamp(override_x,
                                    settings::g_experimentalTabSettings.spoof_game_resolution_override_width.GetMin(),
                                    settings::g_experimentalTabSettings.spoof_game_resolution_override_width.GetMax());
            settings::g_experimentalTabSettings.spoof_game_resolution_override_width.SetValue(override_x);
            settings::g_experimentalTabSettings.spoof_game_resolution_override_width.Save();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Width to report (0 = use game render width).");
        }
        int override_y = settings::g_experimentalTabSettings.spoof_game_resolution_override_height.GetValue();
        if (imgui.InputInt("Override Y", &override_y, 0, 0)) {
            override_y = std::clamp(override_y,
                                    settings::g_experimentalTabSettings.spoof_game_resolution_override_height.GetMin(),
                                    settings::g_experimentalTabSettings.spoof_game_resolution_override_height.GetMax());
            settings::g_experimentalTabSettings.spoof_game_resolution_override_height.SetValue(override_y);
            settings::g_experimentalTabSettings.spoof_game_resolution_override_height.Save();
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Height to report (0 = use game render height). When both X and Y are non-zero, these "
                "values are used; otherwise game render size is used.");
        }
        // Button to request swap chain resize to override (or current render) size
        if (imgui.Button("Resize swap chain to override values")) {
            const HWND hwnd = g_last_swapchain_hwnd.load();
            const int override_w = settings::g_experimentalTabSettings.spoof_game_resolution_override_width.GetValue();
            const int override_h = settings::g_experimentalTabSettings.spoof_game_resolution_override_height.GetValue();
            const int w = (override_w > 0 && override_h > 0) ? override_w : g_game_render_width.load();
            const int h = (override_w > 0 && override_h > 0) ? override_h : g_game_render_height.load();
            if (w > 0 && h > 0 && IsWindow(hwnd)) {
                PostMessage(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(static_cast<UINT>(w), static_cast<UINT>(h)));
                LogInfo("Posted WM_SIZE %dx%d to game window to request swap chain resize", w, h);
            } else {
                LogWarn("Resize swap chain: invalid size (%dx%d) or no game window", w, h);
            }
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Sends WM_SIZE to the game window so the game resizes the swap chain. Uses Override X/Y when both "
                "are non-zero, otherwise uses current game render size.");
        }
        imgui.Unindent();
    }

    imgui.Spacing();

    // Debugger Trigger Button
    if (imgui.Button("Trigger Debugger Break")) {
        LogInfo("Debugger break triggered by user");
        __debugbreak();
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Triggers a debugger breakpoint. Useful for attaching a debugger at a specific moment.\nWARNING: Will "
            "crash if no debugger is attached!");
    }
    imgui.SameLine();

    // Test Crash Handler Button
    if (imgui.Button("Test Crash Handler")) {
        LogInfo("Test crash handler triggered by user - this will cause an intentional crash!");
        // Trigger an intentional access violation to test our crash handler
        int* null_ptr = nullptr;
        *null_ptr = 42;  // This will cause an access violation and trigger our UnhandledExceptionHandler
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Triggers an intentional crash to test the SetUnhandledExceptionFilter spoofing and crash logging "
            "system.\nWARNING: This will crash the application!\nUse this to verify that our exception handler is "
            "working correctly.");
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Unload ReShade DLL Button
    imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), ICON_FK_WARNING " DANGEROUS: Unload ReShade DLL");
    imgui.Spacing();
    if (imgui.Button("Unload ReShade DLL")) {
        LogInfo("User requested to unload ReShade DLL");

        // Find the ReShade module handle
        HMODULE reshade_module = nullptr;
        HMODULE modules[1024];
        DWORD num_modules = 0;

        if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &num_modules) != 0) {
            if (num_modules > sizeof(modules)) {
                num_modules = static_cast<DWORD>(sizeof(modules));
            }

            for (DWORD i = 0; i < num_modules / sizeof(HMODULE); ++i) {
                HMODULE module = modules[i];
                if (module == nullptr) continue;

                // Check if this module has ReShadeRegisterAddon and ReShadeUnregisterAddon
                FARPROC register_func = GetProcAddress(module, "ReShadeRegisterAddon");
                FARPROC unregister_func = GetProcAddress(module, "ReShadeUnregisterAddon");

                if (register_func != nullptr && unregister_func != nullptr) {
                    reshade_module = module;

                    // Get module path for logging
                    wchar_t module_path[MAX_PATH];
                    DWORD path_length = GetModuleFileNameW(module, module_path, MAX_PATH);
                    if (path_length > 0) {
                        char narrow_path[MAX_PATH];
                        WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
                        LogInfo("Found ReShade module: 0x%p - %s", module, narrow_path);
                    } else {
                        LogInfo("Found ReShade module: 0x%p (path unavailable)", module);
                    }
                    break;
                }
            }
        }

        if (reshade_module != nullptr) {
            LogWarn("Attempting to unload ReShade DLL at 0x%p - This may cause a crash!", reshade_module);

            // Store the module path for verification
            wchar_t module_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameW(reshade_module, module_path, MAX_PATH);
            std::string module_path_str;
            if (path_length > 0) {
                char narrow_path[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, module_path, -1, narrow_path, MAX_PATH, nullptr, nullptr);
                module_path_str = narrow_path;
            }

            // Attempt to unload the ReShade DLL by calling FreeLibrary multiple times
            // WARNING: This is very dangerous and will likely crash if ReShade is in use
            // ReShade may be pinned or have multiple references, so we need to try multiple times
            int unload_attempts = 0;
            const int max_attempts = 100;  // Safety limit
            bool still_loaded = true;

            while (still_loaded && unload_attempts < max_attempts) {
                if (FreeLibrary(reshade_module) != 0) {
                    unload_attempts++;
                    // Check if the module is still loaded by trying to get its handle again
                    HMODULE check_module = nullptr;
                    if (path_length > 0) {
                        check_module = GetModuleHandleW(module_path);
                    } else {
                        // If we don't have the path, enumerate modules to check if it's still loaded
                        HMODULE check_modules[1024];
                        DWORD check_num_modules = 0;
                        if (K32EnumProcessModules(GetCurrentProcess(), check_modules, sizeof(check_modules),
                                                  &check_num_modules)
                            != 0) {
                            for (DWORD i = 0; i < check_num_modules / sizeof(HMODULE); ++i) {
                                if (check_modules[i] == reshade_module) {
                                    check_module = reshade_module;
                                    break;
                                }
                            }
                        }
                    }

                    if (check_module == nullptr) {
                        // Module is no longer loaded
                        still_loaded = false;
                        LogWarn("ReShade DLL unloaded successfully after %d FreeLibrary call(s)", unload_attempts);
                        break;
                    }
                } else {
                    // FreeLibrary failed - refcount reached 0, but module might still be loaded if pinned
                    DWORD error = GetLastError();
                    LogWarn("FreeLibrary failed after %d attempt(s), error: %lu", unload_attempts, error);

                    // Check if module is still loaded (might be pinned)
                    HMODULE check_module = nullptr;
                    if (path_length > 0) {
                        check_module = GetModuleHandleW(module_path);
                    } else {
                        HMODULE check_modules[1024];
                        DWORD check_num_modules = 0;
                        if (K32EnumProcessModules(GetCurrentProcess(), check_modules, sizeof(check_modules),
                                                  &check_num_modules)
                            != 0) {
                            for (DWORD i = 0; i < check_num_modules / sizeof(HMODULE); ++i) {
                                if (check_modules[i] == reshade_module) {
                                    check_module = reshade_module;
                                    break;
                                }
                            }
                        }
                    }

                    if (check_module != nullptr) {
                        LogError("ReShade DLL is still loaded - module may be pinned or has other references");
                        LogWarn("The module handle 0x%p is still valid, indicating the DLL was not unloaded",
                                reshade_module);
                    } else {
                        still_loaded = false;
                        LogWarn("ReShade DLL appears to be unloaded despite FreeLibrary failure");
                    }
                    break;
                }
            }

            if (still_loaded) {
                LogError("Failed to unload ReShade DLL after %d attempt(s) - module is likely pinned", unload_attempts);
                LogWarn("ReShade DLL may be pinned (using GetModuleHandleExW with GET_MODULE_HANDLE_EX_FLAG_PIN)");
                LogWarn("Or it may have active references from other code that prevent unloading");
            }

            // Final verification
            HMODULE verify_module = nullptr;
            if (path_length > 0) {
                verify_module = GetModuleHandleW(module_path);
            }
            if (verify_module == nullptr) {
                LogInfo("Verification: ReShade DLL is no longer in the module list");
            } else {
                LogWarn("Verification: ReShade DLL is still loaded at 0x%p", verify_module);
            }
        } else {
            LogError("Failed to find ReShade module - Cannot unload ReShade DLL");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Attempts to unload the ReShade DLL from memory.\n"
            "WARNING: This is extremely dangerous and will likely crash the game!\n"
            "ReShade may still be in use by the game or other addons.\n"
            "Only use this if you understand the risks and are debugging.");
    }

    imgui.Spacing();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                      "Note: Debugger break button will trigger a debugger breakpoint when clicked.");
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                      "Make sure you have a debugger attached before using the debugger break feature.");
    imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                      "WARNING: Crash Handler test will intentionally crash the application!");
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                      "Use it to test our SetUnhandledExceptionFilter spoofing and crash logging system.");
    imgui.TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                      ICON_FK_WARNING " DANGER: Unload ReShade DLL button will attempt to unload ReShade from memory!");
    imgui.TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                      "This is extremely dangerous and will likely crash the game if ReShade is in use!");
}

void DrawHIDSuppression(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "HID Suppression");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Suppress HID input reading for games to prevent them from detecting controllers.\nUseful for preventing "
            "games from interfering with controller input handling.");
    }

    // Master HID suppression enable
    if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_enabled, "Enable HID Suppression", imgui)) {
        LogInfo("HID suppression %s",
                settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Enable/disable HID input suppression for games.");
    }

    // Direct control button
    imgui.SameLine();
    bool current_state = settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue();
    if (imgui.Button("Toggle HID Suppression")) {
        renodx::hooks::SetHIDSuppressionEnabled(!current_state);
        LogInfo("HID suppression toggled via button: %s", !current_state ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Directly toggle HID suppression on/off using the SetHIDSuppressionEnabled function.");
    }

    if (settings::g_experimentalTabSettings.hid_suppression_enabled.GetValue()) {
        imgui.Spacing();

        // DualSense only option
        if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_dualsense_only, "DualSense Only",
                            imgui)) {
            LogInfo(
                "HID suppression DualSense only %s",
                settings::g_experimentalTabSettings.hid_suppression_dualsense_only.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Only suppress DualSense controllers. If disabled, suppresses all HID devices.");
        }

        imgui.Spacing();

        // Individual function blocking options
        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Block Functions:");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Select which HID functions to block for games.");
        }

        if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_block_readfile, "Block ReadFile",
                            imgui)) {
            LogInfo(
                "HID suppression ReadFile blocking %s",
                settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Block ReadFile operations on potential HID devices.");
        }

        if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_block_getinputreport,
                            "Block HidD_GetInputReport", imgui)) {
            LogInfo("HID suppression HidD_GetInputReport blocking %s",
                    settings::g_experimentalTabSettings.hid_suppression_block_getinputreport.GetValue() ? "enabled"
                                                                                                        : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Block HidD_GetInputReport operations for games.");
        }

        if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_block_getattributes,
                            "Block HidD_GetAttributes", imgui)) {
            LogInfo("HID suppression HidD_GetAttributes blocking %s",
                    settings::g_experimentalTabSettings.hid_suppression_block_getattributes.GetValue() ? "enabled"
                                                                                                       : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Block HidD_GetAttributes operations to prevent device detection.");
        }

        if (CheckboxSetting(settings::g_experimentalTabSettings.hid_suppression_block_createfile, "Block CreateFile",
                            imgui)) {
            LogInfo("HID suppression CreateFile blocking %s",
                    settings::g_experimentalTabSettings.hid_suppression_block_createfile.GetValue() ? "enabled"
                                                                                                    : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Block CreateFile operations on HID device paths (\\?\\hid#).");
        }

        imgui.Spacing();

        // Show current settings summary
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Target: %s",
                          settings::g_experimentalTabSettings.hid_suppression_dualsense_only.GetValue()
                              ? "DualSense Only"
                              : "All HID Devices");
        imgui.TextColored(
            ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  ReadFile: %s",
            settings::g_experimentalTabSettings.hid_suppression_block_readfile.GetValue() ? "Blocked" : "Allowed");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  GetInputReport: %s",
                          settings::g_experimentalTabSettings.hid_suppression_block_getinputreport.GetValue()
                              ? "Blocked"
                              : "Allowed");
        imgui.TextColored(
            ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  GetAttributes: %s",
            settings::g_experimentalTabSettings.hid_suppression_block_getattributes.GetValue() ? "Blocked" : "Allowed");
        imgui.TextColored(
            ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  CreateFile: %s",
            settings::g_experimentalTabSettings.hid_suppression_block_createfile.GetValue() ? "Blocked" : "Allowed");

        // Show hook status
        bool hooks_installed = renodx::hooks::AreHIDSuppressionHooksInstalled();
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Hooks Status: %s",
                          hooks_installed ? "Installed" : "Not Installed");

        imgui.Spacing();
        imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          ICON_FK_WARNING " WARNING: This prevents games from reading HID input!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "HID suppression prevents games from reading controller input directly.\nThis may cause games to not "
                "recognize controllers or behave unexpectedly.\nUse with caution and test thoroughly.");
        }
    }
}

void DrawDebugOutputHooks(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Debug Output Hooks ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Hooks OutputDebugStringA/W to log to ReShade!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature hooks Windows debug output functions (OutputDebugStringA/W) and logs their output to the "
            "ReShade log file.\nUseful for debugging games that use debug output for logging or error reporting.");
    }

    imgui.Spacing();

    // Log to ReShade setting
    if (CheckboxSetting(settings::g_experimentalTabSettings.debug_output_log_to_reshade, "Log to ReShade", imgui)) {
        LogInfo("Debug output logging to ReShade %s",
                settings::g_experimentalTabSettings.debug_output_log_to_reshade.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, debug output will be logged to ReShade.log.\nWhen disabled, debug output will only be "
            "passed through to the original functions.");
    }

    // Show statistics setting
    if (CheckboxSetting(settings::g_experimentalTabSettings.debug_output_show_stats, "Show Statistics", imgui)) {
        LogInfo("Debug output statistics display %s",
                settings::g_experimentalTabSettings.debug_output_show_stats.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Display statistics about captured debug output calls in the UI.");
    }

    // Show statistics if enabled
    if (settings::g_experimentalTabSettings.debug_output_show_stats.GetValue()) {
        imgui.Spacing();
        imgui.Separator();
        imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Debug Output Statistics ===");

        auto& stats = display_commanderhooks::debug_output::GetDebugOutputStats();

        imgui.Text("OutputDebugStringA calls: %llu",
                   static_cast<unsigned long long>(stats.output_debug_string_a_calls.load()));
        imgui.Text("OutputDebugStringW calls: %llu",
                   static_cast<unsigned long long>(stats.output_debug_string_w_calls.load()));
        imgui.Text("Total bytes logged: %llu", static_cast<unsigned long long>(stats.total_bytes_logged.load()));

        // Reset statistics button
        if (imgui.Button("Reset Statistics")) {
            stats.output_debug_string_a_calls.store(0);
            stats.output_debug_string_w_calls.store(0);
            stats.total_bytes_logged.store(0);
            LogInfo("Debug output statistics reset");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Reset all debug output statistics to zero.");
        }
    }

    imgui.Spacing();
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                      "Note: This feature captures debug output from OutputDebugStringA and OutputDebugStringW calls.");
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Debug output will appear in ReShade.log when enabled.");
}

void DrawAnisotropicFilteringUpgrade(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "=== Anisotropic Filtering Upgrade ===");
    imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                      ICON_FK_WARNING " EXPERIMENTAL FEATURE - Upgrades linear/bilinear filters to anisotropic!");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "This feature upgrades linear and bilinear texture filters to anisotropic filtering.\n"
            "Anisotropic filtering improves texture quality on surfaces viewed at oblique angles.\n"
            "Use with caution as it may cause performance issues or rendering artifacts in some games.");
    }

    imgui.Spacing();

    // Master enable checkbox
    if (CheckboxSetting(settings::g_experimentalTabSettings.force_anisotropic_filtering,
                        "Enable Anisotropic Filtering Upgrade", imgui)) {
        LogInfo("Anisotropic filtering upgrade %s",
                settings::g_experimentalTabSettings.force_anisotropic_filtering.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Enable automatic upgrade of linear/bilinear filters to anisotropic filtering.\n"
            "The anisotropy level is controlled by the 'Anisotropic Level' setting in the Main tab.");
    }

    if (settings::g_experimentalTabSettings.force_anisotropic_filtering.GetValue()) {
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        imgui.TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Filter Upgrade Options:");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Select which filter types to upgrade to anisotropic filtering.");
        }

        imgui.Spacing();

        // Upgrade trilinear (min_mag_mip_linear) to anisotropic
        if (CheckboxSetting(settings::g_experimentalTabSettings.upgrade_min_mag_mip_linear, "Upgrade Trilinear Filters",
                            imgui)) {
            LogInfo("Upgrade trilinear filters %s",
                    settings::g_experimentalTabSettings.upgrade_min_mag_mip_linear.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Upgrade trilinear filters (min_mag_mip_linear) to full anisotropic filtering.\n"
                "This affects textures that use linear filtering for min, mag, and mip.");
        }

        // Upgrade compare trilinear (compare_min_mag_mip_linear) to compare anisotropic
        if (CheckboxSetting(settings::g_experimentalTabSettings.upgrade_compare_min_mag_mip_linear,
                            "Upgrade Compare Trilinear Filters", imgui)) {
            LogInfo("Upgrade compare trilinear filters %s",
                    settings::g_experimentalTabSettings.upgrade_compare_min_mag_mip_linear.GetValue() ? "enabled"
                                                                                                      : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Upgrade compare trilinear filters (compare_min_mag_mip_linear) to compare anisotropic filtering.\n"
                "This affects shadow samplers that use trilinear filtering.");
        }

        // Upgrade bilinear (min_mag_linear_mip_point) to anisotropic with point mip
        if (CheckboxSetting(settings::g_experimentalTabSettings.upgrade_min_mag_linear_mip_point,
                            "Upgrade Bilinear Filters", imgui)) {
            LogInfo("Upgrade bilinear filters %s",
                    settings::g_experimentalTabSettings.upgrade_min_mag_linear_mip_point.GetValue() ? "enabled"
                                                                                                    : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Upgrade bilinear filters (min_mag_linear_mip_point) to anisotropic with point mip filtering.\n"
                "This preserves point mip filtering while upgrading min/mag to anisotropic.");
        }

        // Upgrade compare bilinear (compare_min_mag_linear_mip_point) to compare anisotropic with point mip
        if (CheckboxSetting(settings::g_experimentalTabSettings.upgrade_compare_min_mag_linear_mip_point,
                            "Upgrade Compare Bilinear Filters", imgui)) {
            LogInfo("Upgrade compare bilinear filters %s",
                    settings::g_experimentalTabSettings.upgrade_compare_min_mag_linear_mip_point.GetValue()
                        ? "enabled"
                        : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Upgrade compare bilinear filters (compare_min_mag_linear_mip_point) to compare anisotropic with point "
                "mip.\n"
                "This affects shadow samplers that use bilinear filtering.");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Show current settings summary
        imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Current Settings:");
        imgui.TextColored(
            ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Trilinear: %s",
            settings::g_experimentalTabSettings.upgrade_min_mag_mip_linear.GetValue() ? "Upgrade" : "Keep Original");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Compare Trilinear: %s",
                          settings::g_experimentalTabSettings.upgrade_compare_min_mag_mip_linear.GetValue()
                              ? "Upgrade"
                              : "Keep Original");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Bilinear: %s",
                          settings::g_experimentalTabSettings.upgrade_min_mag_linear_mip_point.GetValue()
                              ? "Upgrade"
                              : "Keep Original");
        imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Compare Bilinear: %s",
                          settings::g_experimentalTabSettings.upgrade_compare_min_mag_linear_mip_point.GetValue()
                              ? "Upgrade"
                              : "Keep Original");

        // Show anisotropy level from main tab
        int aniso_level = settings::g_mainTabSettings.max_anisotropy.GetValue();
        if (aniso_level > 0) {
            imgui.TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "  Anisotropy Level: %dx", aniso_level);
        } else {
            imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "  Anisotropy Level: 16x (default, set in Main tab)");
        }

        imgui.Spacing();
        imgui.TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                          ICON_FK_WARNING " WARNING: This may cause performance issues or rendering artifacts!");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Upgrading filters to anisotropic may increase GPU load and cause visual artifacts in some games.\n"
                "The anisotropy level is controlled by the 'Anisotropic Level' setting in the Main tab.\n"
                "Set it to 0 in the Main tab to disable anisotropy override (defaults to 16x when upgrading).");
        }
    }
}

void DrawDLLBlockingControls(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Enable/disable DLL blocking feature
    if (CheckboxSetting(settings::g_experimentalTabSettings.dll_blocking_enabled, "Enable DLL Blocking", imgui)) {
        LogInfo("DLL Blocking %s",
                settings::g_experimentalTabSettings.dll_blocking_enabled.GetValue() ? "enabled" : "disabled");

        // Load blocked DLLs if enabling
        if (settings::g_experimentalTabSettings.dll_blocking_enabled.GetValue()) {
            settings::g_experimentalTabSettings.blocked_dlls.Load();
            if (!settings::g_experimentalTabSettings.blocked_dlls.GetValue().empty()) {
                display_commanderhooks::LoadBlockedDLLsFromSettings(
                    settings::g_experimentalTabSettings.blocked_dlls.GetValue());
            }
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Enable DLL blocking feature to prevent specific DLLs from loading.\n"
            "Blocked DLLs will be prevented from loading on next game restart.\n" ICON_FK_WARNING
            " EXPERIMENTAL FEATURE - Use with caution!");
    }

    if (!settings::g_experimentalTabSettings.dll_blocking_enabled.GetValue()) {
        imgui.Unindent();
        return;
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Block DLLs from Loading");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Check the boxes below to prevent specific DLLs from loading.\n"
            "Blocked DLLs will be prevented from loading on next game restart.\n"
            "Settings are automatically saved.");
    }

    imgui.Spacing();

    // Legend
    imgui.TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Legend:");
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "Green");
    imgui.SameLine();
    imgui.Text("= Can be blocked (loaded after Display Commander)");
    imgui.SameLine();
    imgui.TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Gray");
    imgui.SameLine();
    imgui.Text("= Cannot block (loaded before Display Commander)");
    imgui.SameLine();
    imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Red");
    imgui.SameLine();
    imgui.Text("= Blocked");

    imgui.Spacing();

    // Get loaded modules
    static std::vector<display_commanderhooks::ModuleInfo> cached_modules;
    static uint64_t last_update_frame = 0;
    uint64_t current_frame = imgui.GetFrameCount();

    // Update module list every 60 frames (~1 second at 60 FPS)
    if (current_frame - last_update_frame > 60 || cached_modules.empty()) {
        cached_modules = display_commanderhooks::GetLoadedModules();
        last_update_frame = current_frame;
    }

    if (cached_modules.empty()) {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No modules loaded yet");
    } else {
        imgui.TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Loaded Modules (%zu):", cached_modules.size());

        // Show modules in a scrollable child window
        if (imgui.BeginChild("LoadedModules", ImVec2(0, 300), true)) {
            for (const auto& module : cached_modules) {
                std::wstring module_name = module.moduleName;
                if (module_name.empty()) {
                    module_name = L"<unknown>";
                }

                bool is_blocked = display_commanderhooks::IsDLLBlocked(module_name);
                bool can_block = display_commanderhooks::CanBlockDLL(module);

                // Convert to narrow string for display
                std::string narrow_name(module_name.begin(), module_name.end());
                std::string checkbox_id = "##BlockDLL_" + narrow_name;

                // Disable checkbox if module can't be blocked
                if (!can_block) {
                    imgui.BeginDisabled();
                }

                if (imgui.Checkbox(checkbox_id.c_str(), &is_blocked)) {
                    display_commanderhooks::SetDLLBlocked(module_name, is_blocked);

                    // Save to settings
                    std::string blocked_dlls_str = display_commanderhooks::SaveBlockedDLLsToSettings();
                    settings::g_experimentalTabSettings.blocked_dlls.SetValue(blocked_dlls_str);
                    settings::g_experimentalTabSettings.blocked_dlls.Save();

                    LogInfo("DLL %s %s", narrow_name.c_str(), is_blocked ? "blocked" : "unblocked");
                }

                if (!can_block) {
                    imgui.EndDisabled();
                }

                imgui.SameLine();

                // Display module name with color based on status
                if (!can_block) {
                    // Gray out modules that can't be blocked (loaded before Display Commander)
                    imgui.TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", narrow_name.c_str());
                    if (imgui.IsItemHovered()) {
                        std::string full_path(module.fullPath.begin(), module.fullPath.end());
                        imgui.SetTooltipEx("Cannot block: Loaded before Display Commander\nFull path: %s",
                                         full_path.c_str());
                    }
                } else if (is_blocked) {
                    // Red for blocked modules
                    imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", narrow_name.c_str());
                    if (imgui.IsItemHovered()) {
                        std::string full_path(module.fullPath.begin(), module.fullPath.end());
                        imgui.SetTooltipEx("Blocked: Will prevent loading on next restart\nFull path: %s",
                                         full_path.c_str());
                    }
                } else {
                    // Normal color for unblocked modules that can be blocked
                    imgui.TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "%s", narrow_name.c_str());
                    if (imgui.IsItemHovered()) {
                        std::string full_path(module.fullPath.begin(), module.fullPath.end());
                        imgui.SetTooltipEx("Can be blocked: Loaded after Display Commander\nFull path: %s",
                                         full_path.c_str());
                    }
                }
            }
        }
        imgui.EndChild();

        imgui.Spacing();

        // Save button
        if (imgui.SmallButton("Save##BlockedDLLs")) {
            std::string blocked_dlls_str = display_commanderhooks::SaveBlockedDLLsToSettings();
            settings::g_experimentalTabSettings.blocked_dlls.SetValue(blocked_dlls_str);
            settings::g_experimentalTabSettings.blocked_dlls.Save();
            LogInfo("Blocked DLLs saved: %s", blocked_dlls_str.empty() ? "(none)" : blocked_dlls_str.c_str());
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Save the current blocked DLL list to settings");
        }
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Show blocked DLLs that aren't in the loaded modules list
    imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Blocked DLLs (Not Loaded)");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "DLLs that are blocked but haven't been loaded yet.\n"
            "Uncheck to allow them to load on next game restart.");
    }

    imgui.Spacing();

    // Get list of blocked DLLs
    std::vector<std::wstring> blocked_dlls = display_commanderhooks::GetBlockedDLLs();

    // Filter out DLLs that are already in the loaded modules list
    std::vector<std::wstring> blocked_not_loaded;
    for (const auto& blocked_dll : blocked_dlls) {
        bool found_in_loaded = false;
        // Check against cached_modules if available
        if (!cached_modules.empty()) {
            for (const auto& module : cached_modules) {
                std::wstring module_name = module.moduleName;
                if (module_name.empty()) {
                    module_name = L"<unknown>";
                }
                std::wstring lower_module_name = module_name;
                std::transform(lower_module_name.begin(), lower_module_name.end(), lower_module_name.begin(),
                               ::towlower);

                if (lower_module_name == blocked_dll) {
                    found_in_loaded = true;
                    break;
                }
            }
        }
        if (!found_in_loaded) {
            blocked_not_loaded.push_back(blocked_dll);
        }
    }

    if (blocked_not_loaded.empty()) {
        imgui.TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No blocked DLLs (all blocked DLLs are currently loaded)");
    } else {
        imgui.TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Blocked DLLs (%zu):", blocked_not_loaded.size());

        // Show blocked DLLs in a scrollable child window
        if (imgui.BeginChild("BlockedNotLoadedModules", ImVec2(0, 200), true)) {
            for (const auto& blocked_dll : blocked_not_loaded) {
                bool is_blocked = true;  // They're all blocked by definition

                // Convert to narrow string for display
                std::string narrow_name(blocked_dll.begin(), blocked_dll.end());
                std::string checkbox_id = "##UnblockDLL_" + narrow_name;

                if (imgui.Checkbox(checkbox_id.c_str(), &is_blocked)) {
                    // Unblock the DLL
                    display_commanderhooks::SetDLLBlocked(blocked_dll, false);

                    // Save to settings
                    std::string blocked_dlls_str = display_commanderhooks::SaveBlockedDLLsToSettings();
                    settings::g_experimentalTabSettings.blocked_dlls.SetValue(blocked_dlls_str);
                    settings::g_experimentalTabSettings.blocked_dlls.Save();

                    LogInfo("DLL %s unblocked", narrow_name.c_str());
                }

                imgui.SameLine();

                // Display blocked DLL name in red
                imgui.TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", narrow_name.c_str());
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Blocked: Will prevent loading on next restart\nUncheck to allow this DLL to load");
                }
            }
        }
        imgui.EndChild();
    }

    imgui.Unindent();
}

void DrawInputTestTab(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Text("Input Testing - Determine which input APIs the game uses");
    imgui.Separator();
    imgui.Spacing();

    imgui.TextWrapped(
        "Enable individual input blocking methods to test which APIs the game uses for input. "
        "When a method is enabled, that specific input API will be blocked. "
        "If the game stops responding to input when you enable a method, the game likely uses that API.");
    imgui.Spacing();

    // Mouse input testing section
    if (imgui.CollapsingHeader("Mouse Input Testing",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Indent();

        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Mouse Input Blocking Methods:");
        imgui.Spacing();

        // Translate mouse position (window resolution -> render resolution)
        CheckboxSetting(settings::g_experimentalTabSettings.translate_mouse_position, "Translate Mouse Position",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When window resolution is larger than render resolution (e.g. 3840x2160 window, "
                "1920x1080 render), scale mouse coordinates so the game sees render-space coordinates.");
        }
        if (settings::g_experimentalTabSettings.translate_mouse_position.GetValue()) {
            imgui.Indent();
            int override_w = settings::g_experimentalTabSettings.translate_mouse_position_override_width.GetValue();
            if (imgui.InputInt("Override Width", &override_w, 0, 0)) {
                override_w = std::clamp(
                    override_w, settings::g_experimentalTabSettings.translate_mouse_position_override_width.GetMin(),
                    settings::g_experimentalTabSettings.translate_mouse_position_override_width.GetMax());
                settings::g_experimentalTabSettings.translate_mouse_position_override_width.SetValue(override_w);
                settings::g_experimentalTabSettings.translate_mouse_position_override_width.Save();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Width to use for translation (0 = use render width).");
            }
            int override_h = settings::g_experimentalTabSettings.translate_mouse_position_override_height.GetValue();
            if (imgui.InputInt("Override Height", &override_h, 0, 0)) {
                override_h = std::clamp(
                    override_h, settings::g_experimentalTabSettings.translate_mouse_position_override_height.GetMin(),
                    settings::g_experimentalTabSettings.translate_mouse_position_override_height.GetMax());
                settings::g_experimentalTabSettings.translate_mouse_position_override_height.SetValue(override_h);
                settings::g_experimentalTabSettings.translate_mouse_position_override_height.Save();
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Height to use for translation (0 = use render height). When both Width and Height are non-zero, "
                    "these values are used instead of render width/height for mouse position translation.");
            }
            imgui.Unindent();
        }

        const auto game_hwnd = g_last_swapchain_hwnd.load();
        POINT client_topleft = {0, 0};
        if (ClientToScreen(game_hwnd, &client_topleft)) {
            imgui.Text("Client Top Left: %ld, %ld", client_topleft.x, client_topleft.y);
        }
        RECT client_rect = {};
        if (GetClientRect(game_hwnd, &client_rect)) {
            imgui.Text("Client Rect: %ld, %ld, %ld, %ld", client_rect.left, client_rect.top, client_rect.right,
                       client_rect.bottom);
        }
        RECT window_rect = {};
        if (GetWindowRect(game_hwnd, &window_rect)) {
            imgui.Text("Window Rect: %ld, %ld, %ld, %ld", window_rect.left, window_rect.top, window_rect.right,
                       window_rect.bottom);
        }
        // WindoPos
        POINT window_pos;
        const int window_w = client_rect.right - client_rect.left;
        const int window_h = client_rect.bottom - client_rect.top;
        const int render_w = g_game_render_width.load();
        const int render_h = g_game_render_height.load();
        POINT cursor_pos;
        if (display_commanderhooks::GetCursorPos_Original) {
            display_commanderhooks::GetCursorPos_Original(&cursor_pos);
        } else {
            GetCursorPos(&cursor_pos);
        }
        imgui.Text("Cursor Position: %ld, %ld", cursor_pos.x, cursor_pos.y);
        display_commanderhooks::ApplyTranslateMousePositionToCursorPos(&cursor_pos);
        imgui.Text("Translated Cursor Position: %ld, %ld", cursor_pos.x, cursor_pos.y);

        imgui.Text("Game Window: %p", game_hwnd);
        imgui.Text("Window Size: %dx%d", window_w, window_h);
        imgui.Text("Render Size: %dx%d", render_w, render_h);

        imgui.Spacing();

        // Windows Messages
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_messages, "Block Mouse Messages", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Blocks WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, "
                "WM_XBUTTONDOWN, WM_MOUSEWHEEL, WM_MOUSEHWHEEL messages");
        }

        // GetCursorPos
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_getcursorpos, "Block GetCursorPos", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetCursorPos API - returns last known position");
        }

        // SetCursorPos
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_setcursorpos, "Block SetCursorPos", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks SetCursorPos API - prevents cursor position changes");
        }

        // GetKeyState/GetAsyncKeyState for mouse buttons
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_getkeystate, "Block GetKeyState (Mouse)",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetKeyState/GetAsyncKeyState for mouse buttons (VK_LBUTTON, VK_RBUTTON, etc.)");
        }

        // Raw Input
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_rawinput, "Block Raw Input (Mouse)",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetRawInputData/GetRawInputBuffer for mouse input");
        }

        // mouse_event
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_mouseevent, "Block mouse_event", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks mouse_event API");
        }

        // ClipCursor
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_clipcursor, "Block ClipCursor", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks ClipCursor API - prevents cursor clipping");
        }

        // SetCapture/ReleaseCapture
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_mouse_capture, "Block SetCapture", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks SetCapture/ReleaseCapture APIs");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Mouse hook statistics
        imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Mouse Hook Statistics:");
        imgui.Spacing();

        const auto& mouse_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetCursorPos);
        imgui.Text("GetCursorPos: Total=%llu, Unsuppressed=%llu", mouse_stats.total_calls.load(),
                   mouse_stats.unsuppressed_calls.load());

        const auto& setcursor_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_SetCursorPos);
        imgui.Text("SetCursorPos: Total=%llu, Unsuppressed=%llu", setcursor_stats.total_calls.load(),
                   setcursor_stats.unsuppressed_calls.load());

        const auto& keystate_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetKeyState);
        imgui.Text("GetKeyState: Total=%llu, Unsuppressed=%llu", keystate_stats.total_calls.load(),
                   keystate_stats.unsuppressed_calls.load());

        const auto& asynckeystate_stats =
            display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetAsyncKeyState);
        imgui.Text("GetAsyncKeyState: Total=%llu, Unsuppressed=%llu", asynckeystate_stats.total_calls.load(),
                   asynckeystate_stats.unsuppressed_calls.load());

        const auto& rawinput_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetRawInputData);
        imgui.Text("GetRawInputData: Total=%llu, Unsuppressed=%llu", rawinput_stats.total_calls.load(),
                   rawinput_stats.unsuppressed_calls.load());

        const auto& mouseevent_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_mouse_event);
        imgui.Text("mouse_event: Total=%llu, Unsuppressed=%llu", mouseevent_stats.total_calls.load(),
                   mouseevent_stats.unsuppressed_calls.load());

        const auto& clipcursor_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_ClipCursor);
        imgui.Text("ClipCursor: Total=%llu, Unsuppressed=%llu", clipcursor_stats.total_calls.load(),
                   clipcursor_stats.unsuppressed_calls.load());

        const auto& setcapture_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_SetCapture);
        imgui.Text("SetCapture: Total=%llu, Unsuppressed=%llu", setcapture_stats.total_calls.load(),
                   setcapture_stats.unsuppressed_calls.load());

        imgui.Unindent();
    }

    imgui.Spacing();

    // Keyboard input testing section
    if (imgui.CollapsingHeader("Keyboard Input Testing",
                               display_commander::ui::wrapper_flags::TreeNodeFlags_DefaultOpen)) {
        imgui.Indent();

        imgui.TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Keyboard Input Blocking Methods:");
        imgui.Spacing();

        // Windows Messages
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_messages, "Block Keyboard Messages",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_SYSKEYDOWN, WM_SYSKEYUP messages");
        }

        // GetKeyState
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_getkeystate, "Block GetKeyState",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetKeyState API for keyboard keys");
        }

        // GetAsyncKeyState
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_getasynckeystate,
                        "Block GetAsyncKeyState", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetAsyncKeyState API for keyboard keys");
        }

        // GetKeyboardState
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_getkeyboardstate,
                        "Block GetKeyboardState", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetKeyboardState API - clears all keyboard state");
        }

        // Raw Input
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_rawinput, "Block Raw Input (Keyboard)",
                        imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks GetRawInputData/GetRawInputBuffer for keyboard input");
        }

        // keybd_event
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_keybdevent, "Block keybd_event", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks keybd_event API");
        }

        // SendInput
        CheckboxSetting(settings::g_experimentalTabSettings.test_block_keyboard_sendinput, "Block SendInput", imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Blocks SendInput API for keyboard input");
        }

        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        // Keyboard hook statistics
        imgui.TextColored(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), "Keyboard Hook Statistics:");
        imgui.Spacing();

        const auto& keyboard_state_stats =
            display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetKeyboardState);
        imgui.Text("GetKeyboardState: Total=%llu, Unsuppressed=%llu", keyboard_state_stats.total_calls.load(),
                   keyboard_state_stats.unsuppressed_calls.load());

        const auto& kbd_keystate_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetKeyState);
        imgui.Text("GetKeyState: Total=%llu, Unsuppressed=%llu", kbd_keystate_stats.total_calls.load(),
                   kbd_keystate_stats.unsuppressed_calls.load());

        const auto& kbd_asynckeystate_stats =
            display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetAsyncKeyState);
        imgui.Text("GetAsyncKeyState: Total=%llu, Unsuppressed=%llu", kbd_asynckeystate_stats.total_calls.load(),
                   kbd_asynckeystate_stats.unsuppressed_calls.load());

        const auto& kbd_rawinput_stats =
            display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_GetRawInputData);
        imgui.Text("GetRawInputData: Total=%llu, Unsuppressed=%llu", kbd_rawinput_stats.total_calls.load(),
                   kbd_rawinput_stats.unsuppressed_calls.load());

        const auto& keybdevent_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_keybd_event);
        imgui.Text("keybd_event: Total=%llu, Unsuppressed=%llu", keybdevent_stats.total_calls.load(),
                   keybdevent_stats.unsuppressed_calls.load());

        const auto& sendinput_stats = display_commanderhooks::GetHookStats(display_commanderhooks::HOOK_SendInput);
        imgui.Text("SendInput: Total=%llu, Unsuppressed=%llu", sendinput_stats.total_calls.load(),
                   sendinput_stats.unsuppressed_calls.load());

        imgui.Unindent();
    }

    imgui.Spacing();
    imgui.Separator();
    imgui.Spacing();

    // Reset statistics button
    if (imgui.Button("Reset All Hook Statistics")) {
        display_commanderhooks::ResetAllHookStats();
        LogInfo("Reset all hook statistics");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Reset all hook call statistics to zero");
    }
}

}  // namespace ui::new_ui
