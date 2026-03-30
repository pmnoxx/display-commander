#include "experimental_tab.hpp"
#include "../../globals.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/system/timeslowdown_hooks.hpp"
#include "../../nvapi/nvpi_reference.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../ui/imgui_wrapper_reshade.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "../imgui_wrapper_reshade.hpp"
#include "main_new_tab.hpp"
#include "settings_wrapper.hpp"

#include <reshade.hpp>

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

        if (enabled_experimental_features) {
            if (imgui.CollapsingHeader("Time Slowdown Controls",
                                       display_commander::ui::wrapper_flags::TreeNodeFlags_None)) {
                DrawTimeSlowdownControls(imgui);
            }
            imgui.Spacing();
        }

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

}  // namespace ui::new_ui
