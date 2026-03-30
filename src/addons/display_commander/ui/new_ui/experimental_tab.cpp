#include "experimental_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/system/debug_output_hooks.hpp"
#include "../../hooks/system/timeslowdown_hooks.hpp"
#include "../../nvapi/nvpi_reference.hpp"
#include "../../res/forkawesome.h"
#include "../../res/link_libraries.hpp"
#include "../../res/ui_colors.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../ui/imgui_wrapper_reshade.hpp"
#include "../../utils/d3d9_api_version.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_reshade.hpp"
#include "../monitor_settings/monitor_settings.hpp"
#include "main_new_tab.hpp"
#include "settings_wrapper.hpp"

#include <reshade.hpp>

#include <psapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace ui::new_ui {

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

    LogInfo("InitExperimentalTab() - Experimental tab settings loaded and applied to hook system");
}

void DrawExperimentalTab(display_commander::ui::IImGuiWrapper& imgui, reshade::api::effect_runtime* /* runtime */) {
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
            if (imgui.CollapsingHeader("Time Slowdown Controls",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawTimeSlowdownControls(imgui);
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

        if (imgui.CollapsingHeader("Anisotropic Filtering Upgrade",
                                   display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
            DrawAnisotropicFilteringUpgrade(imgui);
        }
        imgui.Spacing();
        imgui.EndTabItem();
    }

    if (imgui.BeginTabItem("Monitor Settings", nullptr, 0)) {
        ui::monitor_settings::DrawMonitorSettings(imgui);
        imgui.EndTabItem();
    }

    imgui.EndTabBar();
}

void CleanupExperimentalTab() {}

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
            "count, FLIPEX, etc.). When disabled, only capture of game "
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

}  // namespace ui::new_ui
