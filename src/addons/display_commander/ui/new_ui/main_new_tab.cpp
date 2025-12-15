#include "main_new_tab.hpp"
#include "../../addon.hpp"
#include "../../adhd_multi_monitor/adhd_simple_api.hpp"
#include "../../audio/audio_management.hpp"
#include "../../latent_sync/latent_sync_limiter.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../performance_types.hpp"
#include "../../settings/developer_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../settings/main_tab_settings.hpp"
#include "../../input_remapping/input_remapping.hpp"
#include "../../widgets/resolution_widget/resolution_widget.hpp"
#include "../../nvapi/reflex_manager.hpp"
#include "../../hooks/nvapi_hooks.hpp"
#include "../../hooks/loadlibrary_hooks.hpp"
#include "../../hooks/windows_hooks/windows_message_hooks.hpp"
#include "../../hooks/window_proc_hooks.hpp"
#include "../../hooks/api_hooks.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/overlay_window_detector.hpp"
#include "../../globals.hpp"
#include "../../swapchain_events.hpp"
#include "imgui.h"
#include "settings_wrapper.hpp"
#include "utils/timing.hpp"
#include "version.hpp"

#include <reshade_imgui.hpp>
#include <minwindef.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d9.h>
#include <d3d9types.h>

// Define IID for IDirect3DDevice9 if not already defined
#ifndef IID_IDirect3DDevice9
EXTERN_C const GUID IID_IDirect3DDevice9 = {0xd0223b96, 0xbf7a, 0x43fd, {0x92, 0xbd, 0xa4, 0x3b, 0x8d, 0x82, 0x9a, 0x7b}};
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <thread>

namespace ui::new_ui {

namespace {
// Flag to indicate a restart is required after changing VSync/tearing options
std::atomic<bool> s_restart_needed_vsync_tearing{false};

// Helper function to check if injected Reflex is active
bool DidNativeReflexSleepRecently(uint64_t now_ns) {
    auto last_injected_call = g_nvapi_last_sleep_timestamp_ns.load();
    return last_injected_call > 0 && (now_ns - last_injected_call) < utils::SEC_TO_NS; // 1s in nanoseconds
}
}  // anonymous namespace

void DrawFrameTimeGraph() {
    // Get frame time data from the performance ring buffer
    const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
    const uint32_t count = (head > static_cast<uint32_t>(::kPerfRingCapacity)) ? static_cast<uint32_t>(::kPerfRingCapacity) : head;

    if (count == 0) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No frame time data available yet...");
        return;
    }

    // Collect frame times for the graph (last 300 samples for smooth display)
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(min(count, 300u));

    const uint32_t start = head - min(count, 300u);
    for (uint32_t i = start; i < head; ++i) {
        const ::PerfSample& sample = ::g_perf_ring[i & (::kPerfRingCapacity - 1)];
        if (sample.dt > 0.0f) {
            frame_times.push_back(sample.dt); // Convert FPS to frame time in ms
        }
    }

    if (frame_times.empty()) {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No valid frame time data available...");
        return;
    }

    // Calculate statistics for the graph
    float min_frame_time = *std::ranges::min_element(frame_times);
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Calculate average FPS from average frame time
    float avg_fps = (avg_frame_time > 0.0f) ? (1.0f / avg_frame_time) : 0.0f;

    // Display statistics
    ImGui::Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms | FPS(avg): %.1f",
                min_frame_time, max_frame_time, avg_frame_time, avg_fps);

    // Create overlay text with current frame time
    std::string overlay_text = "Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";

    // Add sim-to-display latency if GPU measurement is enabled and we have valid data
    if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0 && ::g_sim_to_display_latency_ns.load() > 0) {
        double sim_to_display_ms = (1.0 * ::g_sim_to_display_latency_ns.load() / utils::NS_TO_MS);
        overlay_text += " | Sim-to-Display Lat: " + std::to_string(sim_to_display_ms).substr(0, 4) + " ms";

        // Add GPU late time (how much later GPU finishes compared to Present)
        double gpu_late_ms = (1.0 * ::g_gpu_late_time_ns.load() / utils::NS_TO_MS);
        overlay_text += " | GPU Late: " + std::to_string(gpu_late_ms).substr(0, 4) + " ms";
    }

    // Set graph size and scale
    ImVec2 graph_size = ImVec2(-1.0f, 200.0f); // Full width, 200px height
    float scale_min = 0.0f; // Always start from 0ms
    float scale_max = max(avg_frame_time * 10.f, max_frame_time + 2.0f); // Add some padding

    // Draw the frame time graph
    ImGui::PlotLines("Frame Time (ms)",
                     frame_times.data(),
                     static_cast<int>(frame_times.size()),
                     0, // values_offset
                     overlay_text.c_str(),
                     scale_min,
                     scale_max,
                     graph_size);

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Frame time graph showing recent frame times in milliseconds.\n"
                         "Lower values = higher FPS, smoother gameplay.\n"
                         "Spikes indicate frame drops or stuttering.");
    }

    // Frame Time Mode Selector
    ImGui::Spacing();
    ImGui::Text("Frame Time Mode:");
    ImGui::SameLine();

    int current_mode = static_cast<int>(settings::g_mainTabSettings.frame_time_mode.GetValue());
    const char* mode_items[] = {"Present-to-Present", "Frame Begin-to-Frame Begin", "Display Timing (GPU Completion)"};

    if (ImGui::Combo("##frame_time_mode", &current_mode, mode_items, 3)) {
        settings::g_mainTabSettings.frame_time_mode.SetValue(current_mode);
        LogInfo("Frame time mode changed to: %s", mode_items[current_mode]);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Select which timing events to record for the frame time graph:\n"
                         "- Present-to-Present: Records time between Present calls\n"
                         "- Frame Begin-to-Frame Begin: Records time between frame begin events\n"
                         "- Display Timing: Records when frames are actually displayed (based on GPU completion)\n"
                         "  Note: Display Timing requires GPU measurement to be enabled");
    }
}

// Draw refresh rate frame times graph (display refresh intervals)
void DrawRefreshRateFrameTimesGraph(bool show_tooltips) {
    // Convert refresh rates to frame times (ms) - lock-free iteration
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(256); // Reserve for max samples

    ::dxgi::fps_limiter::ForEachRefreshRateSample([&](double rate) {
        if (rate > 0.0) {
            // Convert Hz to frame time in milliseconds
            frame_times.push_back(static_cast<float>(1000.0 / rate));
        }
    });

    if (frame_times.empty()) {
        return; // Don't show anything if no valid data
    }

    // Calculate statistics for the graph
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Fixed width for overlay (compact)
    ImVec2 graph_size = ImVec2(300.0f, 60.0f); // Fixed 300px width, 60px height
    float scale_min = 0.0f;
    float scale_max = max(avg_frame_time * 5.0f, max_frame_time + 2.0f); // Add some padding but less aggressive

    // Create overlay text with current refresh rate frame time
   //.. std::string overlay_text = "Refresh Frame Time: " + std::to_string(frame_times.back()).substr(0, 4) + " ms";
   // overlay_text += " | Avg: " + std::to_string(avg_frame_time).substr(0, 4) + " ms";

    // Draw chart background with transparency
    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha; // Apply transparency to background

    // Push style color so PlotLines uses the transparent background
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg_color);

    // Draw compact refresh rate frame time graph (line stays fully opaque)
    ImGui::PlotLines("##RefreshRateFrameTime",
                     frame_times.data(),
                     static_cast<int>(frame_times.size()),
                     0, // values_offset
                     nullptr, // overlay_text - no text for compact version
                     scale_min,
                     scale_max,
                     graph_size);

    // Restore original style color
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && show_tooltips) {
        ImGui::SetTooltip("Refresh rate frame time graph showing display refresh intervals in milliseconds.\n"
                         "Lower values = higher refresh rate.\n"
                         "Spikes indicate refresh rate variations (VRR, power management, etc.).");
    }
}

// Compact overlay version with fixed width
void DrawFrameTimeGraphOverlay(bool show_tooltips) {
    // Get frame time data from the performance ring buffer
    const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
    const uint32_t count = (head > static_cast<uint32_t>(::kPerfRingCapacity)) ? static_cast<uint32_t>(::kPerfRingCapacity) : head;

    if (count == 0) {
        return; // Don't show anything if no data
    }
    const uint32_t samples_to_display = min(count, 256u);

    // Collect frame times for the graph (last 256 samples for compact display)
    static std::vector<float> frame_times;
    frame_times.clear();
    frame_times.reserve(samples_to_display);

    for (uint32_t i = 0; i < samples_to_display; ++i) {
        const ::PerfSample& sample = ::g_perf_ring[(head - samples_to_display + i) & (::kPerfRingCapacity - 1)];
        frame_times.push_back(1000.0 * sample.dt); // Convert FPS to frame time in ms
    }

    if (frame_times.empty()) {
        return; // Don't show anything if no valid data
    }

    // Calculate statistics for the graph
    float max_frame_time = *std::ranges::max_element(frame_times);
    float avg_frame_time = 0.0f;
    for (float ft : frame_times) {
        avg_frame_time += ft;
    }
    avg_frame_time /= static_cast<float>(frame_times.size());

    // Fixed width for overlay (compact)
    ImVec2 graph_size = ImVec2(300.0f, 60.0f); // Fixed 300px width, 60px height
    float scale_min = 0.0f;
    float scale_max = max(avg_frame_time * 5.0f, max_frame_time + 2.0f); // Add some padding but less aggressive

    // Draw chart background with transparency
    float chart_alpha = settings::g_mainTabSettings.overlay_chart_alpha.GetValue();
    ImVec4 bg_color = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    bg_color.w *= chart_alpha; // Apply transparency to background

    // Push style color so PlotLines uses the transparent background
    ImGui::PushStyleColor(ImGuiCol_FrameBg, bg_color);

    // Draw compact frame time graph (line stays fully opaque)
    ImGui::PlotLines("##FrameTime",
                     frame_times.data(),
                     static_cast<int>(frame_times.size()),
                     0, // values_offset
                     nullptr, // overlay_text - no text for compact version
                     scale_min,
                     scale_max,
                     graph_size);

    // Restore original style color
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && show_tooltips) {
        ImGui::SetTooltip("Frame time graph (last 256 frames)\nAvg: %.2f ms | Max: %.2f ms", avg_frame_time, max_frame_time);
    }
}

void InitMainNewTab() {
    static bool settings_loaded_once = false;
    if (!settings_loaded_once) {
        // Settings already loaded at startup
        settings::g_mainTabSettings.LoadSettings();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());
        s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
        s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
        // FPS limits are now automatically synced via FloatSettingRef
        // Audio mute settings are automatically synced via BoolSettingRef
        // Background mute settings are automatically synced via BoolSettingRef
        // VSync & Tearing - all automatically synced via BoolSettingRef

        // Apply loaded mute state immediately if manual mute is enabled
        // (BoolSettingRef already synced s_audio_mute, but we need to apply it to the audio system)
        if (settings::g_mainTabSettings.audio_mute.GetValue()) {
            if (::SetMuteForCurrentProcess(true)) {
                ::g_muted_applied.store(true);
                LogInfo("Audio mute state loaded and applied from settings");
            } else {
                LogWarn("Failed to apply loaded mute state");
            }
        }

        // Update input blocking system with loaded settings
        // Input blocking is now handled by Windows message hooks

        // Apply ADHD Multi-Monitor Mode settings
        adhd_multi_monitor::api::SetEnabled(settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue());

        settings_loaded_once = true;

        // FPS limiter mode
        s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(settings::g_mainTabSettings.fps_limiter_mode.GetValue()));
        // Scanline offset and VBlank Sync Divisor are now automatically synced via IntSettingRef

        // Initialize resolution widget
        display_commander::widgets::resolution_widget::InitializeResolutionWidget();

        // Sync log level from settings
        // Note: The setting already updates g_min_log_level via SetValue() when loaded,
        // so we don't need to manually update it here. The setting wrapper handles the
        // index-to-enum conversion automatically.
    }
}

void DrawAdvancedSettings() {

    // Advanced Settings Control
    {
        bool advanced_settings = settings::g_mainTabSettings.advanced_settings_enabled.GetValue();
        if (ImGui::Checkbox(ICON_FK_FILE_CODE " Show All Tabs", &advanced_settings)) {
            settings::g_mainTabSettings.advanced_settings_enabled.SetValue(advanced_settings);
            LogInfo("Advanced settings %s", advanced_settings ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enable advanced settings to show advanced tabs (Developer, Experimental, HID Input, etc.).\n"
                "When disabled, advanced tabs will be hidden to simplify the interface.");
        }
    }

    ImGui::Spacing();

    // Logging Level Control
    // Note: ComboSettingEnumRefWrapper already updates g_min_log_level via SetValue(),
    // so we don't need to manually update it here. Just log the change.
    if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.log_level, "Logging Level")) {
        // Always log the level change (using LogCurrentLogLevel which uses LogError)
        LogCurrentLogLevel();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls the minimum log level to display:\n\n"
            "- Error Only: Only error messages\n"
            "- Warning: Errors and warnings\n"
            "- Info: Errors, warnings, and info messages\n"
            "- Debug (Everything): All log messages (default)");
    }

    ImGui::Spacing();

    // Individual Tab Visibility Settings
    ImGui::Text("Show Individual Tabs:");
    ImGui::Indent();

    if (CheckboxSetting(settings::g_mainTabSettings.show_developer_tab, "Show Developer Tab")) {
        LogInfo("Show Developer tab %s", settings::g_mainTabSettings.show_developer_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Developer tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_window_info_tab, "Show Window Info Tab")) {
        LogInfo("Show Window Info tab %s", settings::g_mainTabSettings.show_window_info_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Window Info tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_swapchain_tab, "Show Swapchain Tab")) {
        LogInfo("Show Swapchain tab %s", settings::g_mainTabSettings.show_swapchain_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Swapchain tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_important_info_tab, "Show Important Info Tab")) {
        LogInfo("Show Important Info tab %s", settings::g_mainTabSettings.show_important_info_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Important Info tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_xinput_tab, "Show XInput Tab")) {
        LogInfo("Show XInput tab %s", settings::g_mainTabSettings.show_xinput_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the XInput controller monitoring tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_remapping_tab, "Show Remapping Tab")) {
        LogInfo("Show Remapping tab %s", settings::g_mainTabSettings.show_remapping_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Controller Remapping tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_hook_stats_tab, "Show Hook Statistics Tab")) {
        LogInfo("Show Hook Statistics tab %s", settings::g_mainTabSettings.show_hook_stats_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Hook Statistics tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_streamline_tab, "Show Streamline Tab")) {
        LogInfo("Show Streamline tab %s", settings::g_mainTabSettings.show_streamline_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Streamline tab even when 'Show All Tabs' is disabled.");
    }

    if (CheckboxSetting(settings::g_mainTabSettings.show_experimental_tab, "Show Experimental Tab")) {
        LogInfo("Show Experimental tab %s", settings::g_mainTabSettings.show_experimental_tab.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Shows the Experimental tab even when 'Show All Tabs' is disabled.");
    }

    ImGui::Unindent();

    ImGui::Spacing();
}

void DrawMainNewTab(reshade::api::effect_runtime* runtime) {
    // Load saved settings once and sync legacy globals

    // Config save failure warning at the top
    auto config_save_failure_path = g_config_save_failure_path.load();
    if (config_save_failure_path != nullptr && !config_save_failure_path->empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_ERROR, ICON_FK_WARNING " Error: Failed to save config to %s", config_save_failure_path->c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The configuration file could not be saved. Check file permissions and disk space.");
        }
        ImGui::Spacing();
    }

    // Version and build information at the top
   // if (ImGui::CollapsingHeader("Display Commander", ImGuiTreeNodeFlags_DefaultOpen))
   {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Version: %s | Build: %s %s", DISPLAY_COMMANDER_VERSION_STRING, DISPLAY_COMMANDER_BUILD_DATE, DISPLAY_COMMANDER_BUILD_TIME);

        // Display current graphics API with feature level/version
        int api_value = g_last_reshade_device_api.load();
        if (api_value != 0) {
            reshade::api::device_api api = static_cast<reshade::api::device_api>(api_value);
            uint32_t api_version = g_last_api_version.load();
            ImGui::SameLine();


            if (api == reshade::api::device_api::d3d9 && s_d3d9e_upgrade_successful.load()) {
                api_version = 0x9100; // due to reshade's bug.
            }

            // Display API with version/feature level if available
            if (api_version != 0) {
                std::string api_string = GetDeviceApiVersionString(api, api_version);
                ImGui::TextColored(ui::colors::TEXT_LABEL, "| Graphics API: %s", api_string.c_str());
            } else {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "| Graphics API: %s", GetDeviceApiString(api));
            }
        }

        // Ko-fi support button
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Support the project:");
        ui::colors::PushIconColor(ui::colors::ICON_SPECIAL);
        if (ImGui::Button(ICON_FK_PLUS " Buy me a coffee on Ko-fi")) {
            ShellExecuteA(nullptr, "open", "https://ko-fi.com/pmnox", nullptr, nullptr, SW_SHOW);
        }
        ui::colors::PopIconColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Support Display Commander development with a coffee!");
        }
    }

    if (enabled_experimental_features) {
        // Gamma Control Warning
        uint32_t gamma_control_calls = g_dxgi_output_event_counters[DXGI_OUTPUT_EVENT_SETGAMMACONTROL].load();
        if (gamma_control_calls > 0) {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " WARNING: Game is using gamma control (SetGammaControl called %u times)", gamma_control_calls);
            LogInfo("TODO: Implement supressing SetGammaControl calls feature.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The game is actively modifying display gamma settings. This may affect color accuracy and HDR functionality. Check the Swapchain tab for more details.");
            }
            ImGui::Spacing();
        }

        // NVIDIA Ansel/Camera SDK Warning (check for all possible DLL names)
        bool ansel_loaded = display_commanderhooks::IsModuleLoaded(L"NvAnselSDK.dll") ||
                        display_commanderhooks::IsModuleLoaded(L"AnselSDK64.dll") ||
                        display_commanderhooks::IsModuleLoaded(L"NvCameraSDK64.dll") ||
                        display_commanderhooks::IsModuleLoaded(L"NvCameraAPI64.dll") ||
                        display_commanderhooks::IsModuleLoaded(L"GFExperienceCore.dll");

        // Also check full paths in case modules are stored with full paths instead of just filenames
        if (!ansel_loaded) {
            auto loaded_modules = display_commanderhooks::GetLoadedModules();
            for (const auto& module : loaded_modules) {
                std::wstring module_name_lower = module.moduleName;
                std::wstring module_path_lower = module.fullPath;
                std::transform(module_name_lower.begin(), module_name_lower.end(), module_name_lower.begin(), ::towlower);
                std::transform(module_path_lower.begin(), module_path_lower.end(), module_path_lower.begin(), ::towlower);

                if (module_name_lower.find(L"anselsdk64.dll") != std::wstring::npos ||
                    module_name_lower.find(L"nvanselsdk.dll") != std::wstring::npos ||
                    module_name_lower.find(L"nvcamerasdk64.dll") != std::wstring::npos ||
                    module_name_lower.find(L"nvcameraapi64.dll") != std::wstring::npos ||
                    module_name_lower.find(L"gfexperiencecore.dll") != std::wstring::npos ||
                    module_path_lower.find(L"anselsdk64.dll") != std::wstring::npos ||
                    module_path_lower.find(L"nvanselsdk.dll") != std::wstring::npos ||
                    module_path_lower.find(L"nvcamerasdk64.dll") != std::wstring::npos ||
                    module_path_lower.find(L"nvcameraapi64.dll") != std::wstring::npos ||
                    module_path_lower.find(L"gfexperiencecore.dll") != std::wstring::npos) {
                    ansel_loaded = true;
                    break;
                }
            }
        }

        // Debug logging for Ansel detection
        static bool debug_logged = false;
        if (!debug_logged) {
            LogInfo("Ansel detection check: NvAnselSDK.dll=%s, AnselSDK64.dll=%s, NvCameraSDK64.dll=%s, NvCameraAPI64.dll=%s, GFExperienceCore.dll=%s",
                    display_commanderhooks::IsModuleLoaded(L"NvAnselSDK.dll") ? "YES" : "NO",
                    display_commanderhooks::IsModuleLoaded(L"AnselSDK64.dll") ? "YES" : "NO",
                    display_commanderhooks::IsModuleLoaded(L"NvCameraSDK64.dll") ? "YES" : "NO",
                    display_commanderhooks::IsModuleLoaded(L"NvCameraAPI64.dll") ? "YES" : "NO",
                    display_commanderhooks::IsModuleLoaded(L"GFExperienceCore.dll") ? "YES" : "NO");

            LogInfo("Fallback Ansel detection result: %s", ansel_loaded ? "YES" : "NO");

            // Also log all loaded modules for debugging
            auto loaded_modules = display_commanderhooks::GetLoadedModules();
            LogInfo("Total loaded modules: %zu", loaded_modules.size());
            for (const auto& module : loaded_modules) {
                // Check if module name contains Ansel, Camera, or GFExperience (case insensitive)
                std::wstring module_name_lower = module.moduleName;
                std::transform(module_name_lower.begin(), module_name_lower.end(), module_name_lower.begin(), ::towlower);

                if (module_name_lower.find(L"ansel") != std::wstring::npos ||
                    module_name_lower.find(L"camera") != std::wstring::npos ||
                    module_name_lower.find(L"gfexperience") != std::wstring::npos) {
                    LogInfo("Found Ansel/Camera related module: %S (path: %S)",
                            module.moduleName.c_str(), module.fullPath.c_str());
                }
            }
            debug_logged = true;
        }


        if (ansel_loaded) {
            ImGui::Spacing();
            bool skip_ansel = settings::g_mainTabSettings.skip_ansel_loading.GetValue();
            if (skip_ansel) {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " WARNING: NVIDIA Ansel/Camera SDK is loaded (Skip enabled but already loaded)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("NVIDIA Ansel/Camera SDK was already loaded before the skip setting could take effect. Restart the game to apply the skip setting.");
                }
            } else {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " WARNING: NVIDIA Ansel/Camera SDK is loaded");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("NVIDIA Ansel/Camera SDK is loaded (NvAnselSDK.dll, AnselSDK64.dll, NvCameraSDK64.dll, NvCameraAPI64.dll, or GFExperienceCore.dll). This may interfere with display settings and HDR functionality. Enable 'Skip Loading Ansel Libraries' to prevent this.");
                }
            }
            ImGui::Spacing();
        }

        // Ansel Control Section
        if (ansel_loaded || settings::g_mainTabSettings.skip_ansel_loading.GetValue()) {
            bool skip_ansel = settings::g_mainTabSettings.skip_ansel_loading.GetValue();
            if (ImGui::Checkbox("Skip Loading Ansel Libraries", &skip_ansel)) {
                settings::g_mainTabSettings.skip_ansel_loading.SetValue(skip_ansel);
                LogInfo("Skip Ansel loading %s", skip_ansel ? "enabled" : "disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevents Ansel-related DLLs from being loaded by the game. This can help avoid conflicts with display settings and HDR functionality. Requires restart to take effect.");
            }

            if (skip_ansel) {
                ImGui::SameLine();
                if (ansel_loaded) {
                    ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Already Loaded");
                } else {
                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, ICON_FK_OK " Active");
                }
            }

            ImGui::Spacing();
        }

        ImGui::Spacing();
    }
    // Display Settings Section
    if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawDisplaySettings(runtime);
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Monitor/Display Resolution Settings Section
    if (ImGui::CollapsingHeader("Resolution Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        display_commander::widgets::resolution_widget::DrawResolutionWidget();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Texture Filtering / Sampler State Section
    if (ImGui::CollapsingHeader("Texture Filtering", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Display call count
        uint32_t d3d11_count = g_d3d_sampler_event_counters[D3D_SAMPLER_EVENT_CREATE_SAMPLER_STATE_D3D11].load();
        uint32_t d3d12_count = g_d3d_sampler_event_counters[D3D_SAMPLER_EVENT_CREATE_SAMPLER_D3D12].load();
        uint32_t total_count = d3d11_count + d3d12_count;

        ImGui::Text("CreateSampler Calls: %u", total_count);
        if (d3d11_count > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "(D3D11: %u)", d3d11_count);
        }
        if (d3d12_count > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "(D3D12: %u)", d3d12_count);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Total number of CreateSamplerState (D3D11) and CreateSampler (D3D12) calls intercepted.");
        }

        // Show statistics if we have any calls
        if (total_count > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Filter Mode Statistics
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Filter Modes (Original Game Requests):");
            ImGui::Indent();

            uint32_t point_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_POINT].load();
            uint32_t linear_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_LINEAR].load();
            uint32_t aniso_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_ANISOTROPIC].load();
            uint32_t comp_point_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_POINT].load();
            uint32_t comp_linear_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_LINEAR].load();
            uint32_t comp_aniso_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_COMPARISON_ANISOTROPIC].load();
            uint32_t other_count = g_sampler_filter_mode_counters[SAMPLER_FILTER_OTHER].load();

            if (point_count > 0) {
                ImGui::Text("  Point: %u", point_count);
            }
            if (linear_count > 0) {
                ImGui::Text("  Linear: %u", linear_count);
            }
            if (aniso_count > 0) {
                ImGui::Text("  Anisotropic: %u", aniso_count);
            }
            if (comp_point_count > 0) {
                ImGui::Text("  Comparison Point: %u", comp_point_count);
            }
            if (comp_linear_count > 0) {
                ImGui::Text("  Comparison Linear: %u", comp_linear_count);
            }
            if (comp_aniso_count > 0) {
                ImGui::Text("  Comparison Anisotropic: %u", comp_aniso_count);
            }
            if (other_count > 0) {
                ImGui::Text("  Other: %u", other_count);
            }

            ImGui::Unindent();
            ImGui::Spacing();

            // Address Mode Statistics
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Address Modes (U Coordinate):");
            ImGui::Indent();

            uint32_t wrap_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_WRAP].load();
            uint32_t mirror_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_MIRROR].load();
            uint32_t clamp_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_CLAMP].load();
            uint32_t border_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_BORDER].load();
            uint32_t mirror_once_count = g_sampler_address_mode_counters[SAMPLER_ADDRESS_MIRROR_ONCE].load();

            if (wrap_count > 0) {
                ImGui::Text("  Wrap: %u", wrap_count);
            }
            if (mirror_count > 0) {
                ImGui::Text("  Mirror: %u", mirror_count);
            }
            if (clamp_count > 0) {
                ImGui::Text("  Clamp: %u", clamp_count);
            }
            if (border_count > 0) {
                ImGui::Text("  Border: %u", border_count);
            }
            if (mirror_once_count > 0) {
                ImGui::Text("  Mirror Once: %u", mirror_once_count);
            }

            ImGui::Unindent();
            ImGui::Spacing();

            // Anisotropy Level Statistics (only for anisotropic filters)
            uint32_t total_aniso_samplers = 0;
            for (int i = 0; i < MAX_ANISOTROPY_LEVELS; ++i) {
                total_aniso_samplers += g_sampler_anisotropy_level_counters[i].load();
            }

            if (total_aniso_samplers > 0) {
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Anisotropic Filtering Levels (Original Game Requests):");
                ImGui::Indent();

                // Show only levels that have been used
                for (int i = 0; i < MAX_ANISOTROPY_LEVELS; ++i) {
                    uint32_t count = g_sampler_anisotropy_level_counters[i].load();
                    if (count > 0) {
                        int level = i + 1; // Convert from 0-based index to 1-based level
                        ImGui::Text("  %dx: %u", level, count);
                    }
                }

                ImGui::Unindent();
                ImGui::Spacing();
            }

            ImGui::Separator();
        }

        ImGui::Spacing();

        // Max Anisotropy Override
        // Only affects existing anisotropic filters
        int max_aniso = settings::g_mainTabSettings.max_anisotropy.GetValue();
        if (ImGui::SliderInt("Anisotropic Level", &max_aniso, 0, 16, max_aniso == 0 ? "Game default" : "%dx")) {
            settings::g_mainTabSettings.max_anisotropy.SetValue(max_aniso);
            LogInfo("Max anisotropy set to %d", max_aniso);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Override maximum anisotropic filtering level (1-16) for existing anisotropic filters.\n"
                              "Set to 0 (Game default) to preserve the game's original AF settings.\n"
                              "Only affects samplers that already use anisotropic filtering.");
        }

        // Reset button for Anisotropic Level
        if (max_aniso != 0) {
            ImGui::SameLine();
            if (ImGui::Button("Game Default")) {
                settings::g_mainTabSettings.max_anisotropy.SetValue(0);
                LogInfo("Max anisotropy reset to game default");
            }
        }

        ImGui::Spacing();

        // Mipmap LOD Bias
        float lod_bias = settings::g_mainTabSettings.force_mipmap_lod_bias.GetValue();
        if (ImGui::SliderFloat("Mipmap LOD Bias", &lod_bias, -5.0f, 5.0f, lod_bias == 0.0f ? "Game Default" : "%.2f")) {
            settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(lod_bias);
            LogInfo("Mipmap LOD bias set to %.2f", lod_bias);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use a small (i.e. -0.6'ish) negative LOD bias to sharpen DLSS and FSR games");
        }

        // Reset button for LOD bias
        if (lod_bias != 0.0f) {
            ImGui::SameLine();
            if (ImGui::Button("Game Default")) {
                settings::g_mainTabSettings.force_mipmap_lod_bias.SetValue(0.0f);
                LogInfo("Mipmap LOD bias reset to game default");
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Game restart may be required for changes to take full effect.");

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Audio Settings Section
    if (ImGui::CollapsingHeader("Audio Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawAudioSettings();
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Input Blocking Section
    if (ImGui::CollapsingHeader("Input Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        // Create 3 columns with fixed width
        ImGui::Columns(3, "InputBlockingColumns", true);

        // First line: Headers
        ImGui::Text("Suppress Keyboard");
        ImGui::NextColumn();
        ImGui::Text("Suppress Mouse");
        ImGui::NextColumn();
        ImGui::Text("Suppress Gamepad");
        ImGui::NextColumn();

        // Second line: Selectors
        if (ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.keyboard_input_blocking, "##Keyboard")) {
            // Restore cursor clipping when input blocking is disabled
            if (settings::g_mainTabSettings.keyboard_input_blocking.GetValue() == static_cast<int>(InputBlockingMode::kDisabled)) {
                display_commanderhooks::RestoreClipCursor();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls keyboard input blocking behavior.");
        }

        ImGui::NextColumn();

        if (ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.mouse_input_blocking, "##Mouse")) {
            // Restore cursor clipping when input blocking is disabled
            if (settings::g_mainTabSettings.mouse_input_blocking.GetValue() == static_cast<int>(InputBlockingMode::kDisabled)) {
                display_commanderhooks::RestoreClipCursor();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls mouse input blocking behavior.");
        }

        ImGui::NextColumn();

        ui::new_ui::ComboSettingEnumRefWrapper(settings::g_mainTabSettings.gamepad_input_blocking, "##Gamepad");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls gamepad input blocking behavior.");
        }

        ImGui::Columns(1); // Reset to single column

        ImGui::Spacing();

        // Home button behavior for Display Commander UI
        bool require_solo_press = settings::g_mainTabSettings.guide_button_solo_ui_toggle_only.GetValue();
        if (ImGui::Checkbox("Require Home-only press to toggle Display Commander UI", &require_solo_press)) {
            settings::g_mainTabSettings.guide_button_solo_ui_toggle_only.SetValue(require_solo_press);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, tapping the Home button will open/close Display Commander UI only if no other\n"
                "gamepad buttons were pressed between Home down and Home up.\n\n"
                "Example:\n"
                "- Press Home, do nothing else, release Home -> Toggle Display Commander UI\n"
                "- Press Home + any other button (e.g. volume chords) -> Do NOT toggle Display Commander UI");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Screensaver Control Section
    if (ImGui::CollapsingHeader("Screensaver Control", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.screensaver_mode, "Screensaver Mode")) {
            LogInfo("Screensaver mode changed to %d", settings::g_mainTabSettings.screensaver_mode.GetValue());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Controls screensaver behavior while the game is running:\n\n"
                "- Default (no change): Preserves original game behavior\n"
                "- Disable when Focused: Disables screensaver when game window is focused\n"
                "- Disable: Always disables screensaver while game is running\n\n"
                "Note: This feature requires the screensaver implementation to be active.");
        }
        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Window Controls Section
    DrawWindowControls();

    ImGui::Spacing();

    // Overlay Windows Detection Section
    if (ImGui::CollapsingHeader("Overlay Windows", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        HWND game_window = display_commanderhooks::GetGameWindow();
        if (game_window != nullptr && IsWindow(game_window) != FALSE) {
            static DWORD last_check_time = 0;
            DWORD current_time = GetTickCount();

            // Update overlay list every 500ms to avoid performance impact
            static std::vector<display_commander::utils::OverlayWindowInfo> overlay_list;
            if (current_time - last_check_time > 500) {
                overlay_list = display_commander::utils::DetectOverlayWindows(game_window);
                last_check_time = current_time;
            }

            if (overlay_list.empty()) {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "No overlay windows detected");
            } else {
                ImGui::Text("Detected %zu overlay window(s):", overlay_list.size());
                ImGui::Spacing();

                // Create a table-like display
                if (ImGui::BeginTable("OverlayWindows", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                    ImGui::TableSetupColumn("Window Title", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("Z-Order", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Overlap Area", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& overlay : overlay_list) {
                        ImGui::TableNextRow();

                        // Window Title
                        ImGui::TableSetColumnIndex(0);
                        std::string title_utf8 = overlay.window_title.empty() ?
                            "(No Title)" :
                            std::string(overlay.window_title.begin(), overlay.window_title.end());
                        ImGui::TextUnformatted(title_utf8.c_str());

                        // Process Name
                        ImGui::TableSetColumnIndex(1);
                        std::string process_utf8 = overlay.process_name.empty() ?
                            "(Unknown)" :
                            std::string(overlay.process_name.begin(), overlay.process_name.end());
                        ImGui::TextUnformatted(process_utf8.c_str());

                        // PID
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%lu", overlay.process_id);

                        // Z-Order (Above/Below)
                        ImGui::TableSetColumnIndex(3);
                        if (overlay.is_above_game) {
                            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                            ImGui::Text(ICON_FK_WARNING " Above");
                            ui::colors::PopIconColor();
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Below");
                        }

                        // Overlapping Area
                        ImGui::TableSetColumnIndex(4);
                        if (overlay.overlaps_game) {
                            ImGui::Text("%ld px (%.1f%%)",
                                       overlay.overlapping_area_pixels,
                                       overlay.overlapping_area_percent);
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "No overlap");
                        }

                        // Status
                        ImGui::TableSetColumnIndex(5);
                        if (overlay.overlaps_game) {
                            ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                            ImGui::Text(ICON_FK_WARNING " Overlapping");
                            ui::colors::PopIconColor();
                        } else if (overlay.is_visible) {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Visible");
                        } else {
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Hidden");
                        }
                    }

                    ImGui::EndTable();
                }
            }
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Game window not detected");
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows all visible windows that overlap with the game window.\n"
                             "Windows can be above or below the game in Z-order.\n"
                             "Overlapping windows may cause performance issues.");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Background Rendering Section
    if (ImGui::CollapsingHeader("Background Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();

        // Continue Rendering
        if (CheckboxSetting(settings::g_developerTabSettings.continue_rendering, "Continue Rendering in Background")) {
            s_continue_rendering.store(settings::g_developerTabSettings.continue_rendering.GetValue());
            LogInfo("Continue rendering in background %s",
                    settings::g_developerTabSettings.continue_rendering.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Prevent games from pausing or reducing performance when alt-tabbed. Blocks window focus "
                "messages to keep games running in background.");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Important Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        DrawImportantInfo();
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Advanced Settings", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();
        DrawAdvancedSettings();
        ImGui::Unindent();
    }
}

void DrawQuickResolutionChanger() {
    // Quick-set buttons based on current monitor refresh rate
    {
        double refresh_hz;
        {
            auto window_state = ::g_window_state.load();
            refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
        }
        int y = static_cast<int>(std::round(refresh_hz));
        if (y > 0) {
            bool first = true;
            const float selected_epsilon = 0.0001f;
            // Add No Limit button at the beginning
            {
                bool selected =
                    (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - 0.0f) <= selected_epsilon);
                if (selected) {
                    ui::colors::PushSelectedButtonColors();
                }
                if (ImGui::Button("No Limit")) {
                    settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
                }
                if (selected) {
                    ui::colors::PopSelectedButtonColors();
                }
            }
            first = false;
            for (int x = 1; x <= 15; ++x) {
                if (y % x == 0) {
                    int candidate_rounded = y / x;
                    float candidate_precise = refresh_hz / x;
                    if (candidate_rounded >= 30) {
                        if (!first) ImGui::SameLine();
                        first = false;
                        std::string label = std::to_string(candidate_rounded);
                        {
                            bool selected =
                                (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - candidate_precise)
                                 <= selected_epsilon);
                            if (selected) {
                                ui::colors::PushSelectedButtonColors();
                            }
                            if (ImGui::Button(label.c_str())) {
                                float target_fps = candidate_precise;
                                settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                            }
                            if (selected) {
                                ui::colors::PopSelectedButtonColors();
                            }
                            // Add tooltip showing the precise calculation
                            if (ImGui::IsItemHovered()) {
                                std::ostringstream tooltip_oss;
                                tooltip_oss.setf(std::ios::fixed);
                                tooltip_oss << std::setprecision(3);
                                tooltip_oss << "FPS = " << refresh_hz << " ÷ " << x << " = " << candidate_precise
                                            << " FPS\n\n";
                                tooltip_oss << "Creates a smooth frame rate that divides evenly\n";
                                tooltip_oss << "into the monitor's refresh rate.";
                                ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                            }
                        }
                    }
                }
            }
            // Add Gsync Cap button at the end
            if (!first) {
                ImGui::SameLine();
            }

            {
                // Gsync formula: refresh_hz - (refresh_hz * refresh_hz / 3600)
                double gsync_target = refresh_hz - (refresh_hz * refresh_hz / 3600.0);
                float precise_target = static_cast<float>(gsync_target);
                if (precise_target < 1.0f) precise_target = 1.0f;
                bool selected =
                    (std::fabs(settings::g_mainTabSettings.fps_limit.GetValue() - precise_target) <= selected_epsilon);

                if (selected) {
                    ui::colors::PushSelectedButtonColors();
                }
                if (ImGui::Button("Gsync Cap")) {
                    double precise_target = gsync_target;  // do not round on apply
                    float target_fps = static_cast<float>(precise_target < 1.0 ? 1.0 : precise_target);
                    settings::g_mainTabSettings.fps_limit.SetValue(target_fps);
                }
                if (selected) {
                    ui::colors::PopSelectedButtonColors();
                }
                // Add tooltip explaining the Gsync formula
                if (ImGui::IsItemHovered()) {
                    std::ostringstream tooltip_oss;
                    tooltip_oss.setf(std::ios::fixed);
                    tooltip_oss << std::setprecision(3);
                    tooltip_oss << "Gsync Cap: FPS = " << refresh_hz << " - (" << refresh_hz << "² / 3600)\n";
                    tooltip_oss << "= " << refresh_hz << " - " << (refresh_hz * refresh_hz / 3600.0) << " = "
                                << gsync_target << " FPS\n\n";
                    tooltip_oss << "Creates a ~0.3ms frame time buffer to optimize latency\n";
                    tooltip_oss << "and prevent tearing, similar to NVIDIA Reflex Low Latency Mode.";
                    ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
                }
            }
        }
    }
}

void DrawDisplaySettings(reshade::api::effect_runtime* runtime) {
    assert(runtime != nullptr);
    auto native_device = reinterpret_cast<IUnknown*>(runtime->get_device()->get_native());

    auto now = utils::get_now_ns();
    {
        // Target Display dropdown
        // Use device ID-based approach for better reliability

        auto display_info = display_cache::g_displayCache.GetDisplayInfoForUI();
        std::vector<const char*> monitor_c_labels;
        monitor_c_labels.reserve(display_info.size());
        for (const auto& info : display_info) {
            monitor_c_labels.push_back(info.display_label.c_str());
        }

        // Find current selection by device ID
        std::string current_device_id = settings::g_mainTabSettings.selected_extended_display_device_id.GetValue();
        int selected_index = 0; // Default to first display
        for (size_t i = 0; i < display_info.size(); ++i) {
            if (display_info[i].extended_device_id == current_device_id) {
                selected_index = static_cast<int>(i);
                break;
            }
        }

        if (ImGui::Combo("Target Display", &selected_index, monitor_c_labels.data(),
                         static_cast<int>(monitor_c_labels.size()))) {
            if (selected_index >= 0 && selected_index < static_cast<int>(display_info.size())) {
                // Store the device ID
                std::string new_device_id = display_info[selected_index].extended_device_id;
                settings::g_mainTabSettings.selected_extended_display_device_id.SetValue(new_device_id);

                LogInfo("Target monitor changed to device ID: %s", new_device_id.c_str());
            }
        }
        if (ImGui::IsItemHovered()) {
            // Get the saved game window display device ID for tooltip
            std::string saved_device_id = settings::g_mainTabSettings.game_window_display_device_id.GetValue();
            std::string tooltip_text =
                "Choose which monitor to apply size/pos to. The monitor corresponding to the "
                "game window is automatically selected.";
            if (!saved_device_id.empty() && saved_device_id != "No Window" && saved_device_id != "No Monitor"
                && saved_device_id != "Monitor Info Failed") {
                tooltip_text += "\n\nGame window is on: " + saved_device_id;
            }
            ImGui::SetTooltip("%s", tooltip_text.c_str());
        }

    }

    // Window Mode dropdown (with persistent setting)
    if (ComboSettingEnumRefWrapper(settings::g_mainTabSettings.window_mode, "Window Mode")) {
        WindowMode old_mode = s_window_mode.load();
        s_window_mode = static_cast<WindowMode>(settings::g_mainTabSettings.window_mode.GetValue());

        // Don't apply changes immediately - let the normal window management system handle it
        // This prevents crashes when changing modes during gameplay

        std::ostringstream oss;
        oss << "Window mode changed from " << static_cast<int>(old_mode) << " to "
            << settings::g_mainTabSettings.window_mode.GetValue();
        LogInfo(oss.str().c_str());
    }

    // Aspect Ratio dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.aspect_index, "Aspect Ratio")) {
            s_aspect_index = static_cast<AspectRatioType>(settings::g_mainTabSettings.aspect_index.GetValue());
            LogInfo("Aspect ratio changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose the aspect ratio for window resizing.");
        }
    }
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        // Width dropdown for aspect ratio mode
        if (ComboSettingRefWrapper(settings::g_mainTabSettings.window_aspect_width, "Window Width")) {
            s_aspect_width.store(settings::g_mainTabSettings.window_aspect_width.GetValue());
            LogInfo("Window width for aspect mode setting changed to: %d", s_aspect_width.load());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose the width for the aspect ratio window. 'Display Width' uses the current monitor width.");
        }
    }

    // Window Alignment dropdown (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (ComboSettingWrapper(settings::g_mainTabSettings.alignment, "Alignment")) {
            s_window_alignment = static_cast<WindowAlignment>(settings::g_mainTabSettings.alignment.GetValue());
            LogInfo("Window alignment changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Choose how to align the window when repositioning is needed. 0=Center, 1=Top Left, "
                "2=Top Right, 3=Bottom Left, 4=Bottom Right.");
        }
    }
    // Background Black Curtain checkbox (only shown in Aspect Ratio mode)
    if (s_window_mode.load() == WindowMode::kAspectRatio) {
        if (CheckboxSetting(settings::g_mainTabSettings.background_feature, "Background Black Curtain")) {
            LogInfo("Background black curtain setting changed");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Creates a black background behind the game window when it doesn't cover the full screen.");
        }
        ImGui::SameLine();
    }

    // ADHD Multi-Monitor Mode controls
    DrawAdhdMultiMonitorControls(s_window_mode.load() == WindowMode::kAspectRatio);

    // Apply Changes button
    ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
    if (ImGui::Button(ICON_FK_OK " Apply Changes")) {
        // Force immediate application of window changes
        ::g_init_apply_generation.fetch_add(1);
        LogInfo("Apply Changes button clicked - forcing immediate window update");
        std::ostringstream oss;
        // All global settings on this tab are handled by the settings wrapper
        oss << "Apply Changes button clicked - forcing immediate window update";
        LogInfo(oss.str().c_str());
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Apply the current window size and position settings immediately.");
    }

    ImGui::Spacing();

    // FPS Limiter Mode
    {
        const char* items[] = {
            "Disabled",
            "NVIDIA Reflex (low latency mode + boost) VRR DX11/DX12 (DLSS-FG aware)",
            "Sync frame Present/Start Time (adds latency to offer more consistent frame timing) VRR/Non-VRR",
            "Sync to Display Refresh Rate (fraction of monitor refresh rate) Non-VRR",
            "Non-Reflex Low Latency Mode (not implemented) VRR"
        };

        int current_item = settings::g_mainTabSettings.fps_limiter_mode.GetValue();
        int prev_item = current_item;
        if (ImGui::Combo("FPS Limiter Mode", &current_item, items, 5)) {
            settings::g_mainTabSettings.fps_limiter_mode.SetValue(current_item);
            s_fps_limiter_mode.store(static_cast<FpsLimiterMode>(current_item));
            FpsLimiterMode mode = s_fps_limiter_mode.load();
            if (mode == FpsLimiterMode::kDisabled) {
                LogInfo("FPS Limiter: Disabled (no limiting)");
            } else if (mode == FpsLimiterMode::kReflex) {
                LogInfo("FPS Limiter: Reflex");
                s_reflex_auto_configure.store(true);
                settings::g_developerTabSettings.reflex_auto_configure.SetValue(true);
            } else if (mode == FpsLimiterMode::kOnPresentSync) {
                LogInfo("FPS Limiter: OnPresent Frame Synchronizer");
            } else if (mode == FpsLimiterMode::kLatentSync) {
                LogInfo("FPS Limiter: VBlank Scanline Sync for VSYNC-OFF or without VRR");
            } else if (mode == FpsLimiterMode::kNonReflexLowLatency) {
                LogInfo("FPS Limiter: Non-Reflex Low Latency Mode - Not implemented yet");
            }

            if (mode == FpsLimiterMode::kReflex && prev_item != static_cast<int>(FpsLimiterMode::kReflex)) {
                // reset the reflex auto configure setting
                settings::g_developerTabSettings.reflex_auto_configure.SetValue(false);
                s_reflex_auto_configure.store(false);
            }
        }

        // Custom rendering for grayed out option
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose limiter: OnPresent Frame Synchronizer (synchronized frame display timing) or VBlank Scanline Sync\n\nOnPresent Frame Synchronizer adds latency as it delays frame display time to be more consistent - it prioritizes starting frame processing at the same time.\n\nVBlank Scanline Sync synchronizes frame presentation with monitor refresh cycles for smooth frame pacing without VSync.");
        }
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync) || current_item == static_cast<int>(FpsLimiterMode::kLatentSync) || current_item == static_cast<int>(FpsLimiterMode::kNonReflexLowLatency)) {
            // if dlls-g is enabled warn that reflex fps limiter should be used, because this mode isn't aware of native frames

            if (g_dlssg_enabled.load()) {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Warning: DLLS-G is enabled. Reflex FPS Limiter should be used instead of this mode.");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("DLLS-G is enabled. Reflex FPS Limiter should be used instead of this mode.");
                }
            }
        }

        if (current_item == static_cast<int>(FpsLimiterMode::kReflex)) {
            // Check if we're running on D3D9 and show warning
            int current_api = g_last_reshade_device_api.load();
            if (current_api == static_cast<int>(reshade::api::device_api::d3d9)) {
                ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Warning: Reflex does not work with Direct3D 9");
            } else {
                uint64_t now_ns = utils::get_now_ns();
                if (IsNativeReflexActive(now_ns)) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Native Reflex: ACTIVE Native Frame Pacing: ON");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "The game has native Reflex support and is actively using it. "
                            "Do not enable addon Reflex features to avoid conflicts.");
                    }
                    double native_ns = static_cast<double>(g_sleep_reflex_native_ns_smooth.load());
                    double calls_per_second = native_ns <= 0 ? -1 : 1000000000.0 / native_ns;
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Native Reflex: %.2f times/sec (%.1f ms interval)", calls_per_second, native_ns / 1000000.0);
                    if (ImGui::IsItemHovered()) {
                        double raw_ns = static_cast<double>(g_sleep_reflex_native_ns.load());
                        ImGui::SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms", raw_ns / 1000000.0);
                    }
                    if (!DidNativeReflexSleepRecently(now_ns)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING " Warning: Native Reflex is not sleeping recently - may indicate issues! (FIXME)");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Injected Reflex: ACTIVE Native Frame Pacing: OFF");
                    double injected_ns = static_cast<double>(g_sleep_reflex_injected_ns_smooth.load());
                    double calls_per_second = injected_ns <= 0 ? -1 : 1000000000.0 / injected_ns;
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Injected Reflex: %.2f times/sec (%.1f ms interval)", calls_per_second, injected_ns / 1000000.0);
                    if (ImGui::IsItemHovered()) {
                        double raw_ns = static_cast<double>(g_sleep_reflex_injected_ns.load());
                        ImGui::SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms", raw_ns / 1000000.0);
                    }


                    // Warn if both native and injected reflex are running simultaneously
                    if (DidNativeReflexSleepRecently(now_ns)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING " Warning: Both native and injected Reflex are active - this may cause conflicts! (FIXME)");
                    }
                }

                // Show Boost checkbox for both native and injected Reflex
                ImGui::Spacing();
                #if 0
                bool reflex_low_latency = settings::g_developerTabSettings.reflex_low_latency.GetValue();
                if (ImGui::Checkbox("Low Latency Mode", &reflex_low_latency)) {
                    settings::g_developerTabSettings.reflex_low_latency.SetValue(reflex_low_latency);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enables NVIDIA Reflex Low Latency Mode to reduce input lag and system latency.\nThis helps improve responsiveness in competitive gaming scenarios.");
                }
                ImGui::SameLine();
                #endif
                bool reflex_boost = settings::g_developerTabSettings.reflex_boost.GetValue();
                if (ImGui::Checkbox("Boost", &reflex_boost)) {
                    settings::g_developerTabSettings.reflex_boost.SetValue(reflex_boost);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Enables NVIDIA Reflex Boost mode for maximum latency reduction.\nThis mode may increase GPU power consumption but provides the lowest possible input lag.");
                }
            }
            if (IsNativeReflexActive() || settings::g_developerTabSettings.reflex_supress_native.GetValue()) {
                ImGui::SameLine();
                if (CheckboxSetting(settings::g_developerTabSettings.reflex_supress_native, ICON_FK_WARNING " Suppress Native Reflex (WIP)")) {
                    LogInfo("Suppress Native Reflex %s", settings::g_developerTabSettings.reflex_supress_native.GetValue() ? "enabled" : "disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Override the game's native Reflex implementation with the addon's injected version.");
                }
            }
        }

        // Show warning for non-implemented low latency mode
        if (current_item == static_cast<int>(FpsLimiterMode::kNonReflexLowLatency)) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Native Frame Pacing: OFF");
            ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Non-Reflex Low Latency Mode not implemented yet");
        }

        // Present Pacing Delay slider (persisted)
        if (current_item == static_cast<int>(FpsLimiterMode::kOnPresentSync)) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Native Frame Pacing: OFF");

            ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "Present Pacing Delay:");
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Improves frame timing consistency");

            ImGui::TextColored(ui::colors::TEXT_SUBTLE,
                            "Adds a small delay after present to smooth frame pacing and reduce stuttering");

            float current_delay = settings::g_mainTabSettings.present_pacing_delay_percentage.GetValue();
            if (SliderFloatSettingRef(settings::g_mainTabSettings.present_pacing_delay_percentage,
                                "Present Pacing Delay", "%.1f%%")) {
                // The setting is automatically synced via FloatSettingRef
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Present Pacing Delay: Adds delay to starting next frame.\n\n"
                    "How it reduces latency:\n"
                    "- Allow for more time for CPU to process input.\n"
                    "- Lower values provide more consistent frame timing.\n"
                    "- Higher values provide lower latency but slightly less consistent timing.\n"
                    "Range: 0%% to 100%%. Default: 0%% (1 frame time delay).\n"
                    "Manual fine-tuning required.");
            }

            // Add question mark with tooltip for manual fine-tuning note
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::ICON_WARNING, ICON_FK_WARNING);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Manual fine-tuning is needed for now");
            }
        }
    }

    // Latent Sync Mode (only visible if Latent Sync limiter is selected)
    if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
        // Scanline Offset (only visible if scanline mode is selected)
        int current_offset = settings::g_mainTabSettings.scanline_offset.GetValue();
        int temp_offset = current_offset;
        if (ImGui::SliderInt("Scanline Offset", &temp_offset, -1000, 1000, "%d")) {
            settings::g_mainTabSettings.scanline_offset.SetValue(temp_offset);
            s_scanline_offset.store(temp_offset);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Scanline offset for latent sync (-1000 to 1000). This defines the offset from the "
                "threshold where frame pacing is active.");
        }

        // VBlank Sync Divisor (only visible if latent sync mode is selected)
        int current_divisor = settings::g_mainTabSettings.vblank_sync_divisor.GetValue();
        int temp_divisor = current_divisor;
        if (ImGui::SliderInt("VBlank Sync Divisor (controls FPS limit as fraction of monitor refresh rate)", &temp_divisor, 0, 8, "%d")) {
            settings::g_mainTabSettings.vblank_sync_divisor.SetValue(temp_divisor);
            s_vblank_sync_divisor.store(temp_divisor);
        }
        if (ImGui::IsItemHovered()) {
            // Calculate effective refresh rate based on monitor info
            auto window_state = ::g_window_state.load();
            double refresh_hz = 60.0;  // default fallback
            if (window_state) {
                refresh_hz = window_state->current_monitor_refresh_rate.ToHz();
            }

            std::ostringstream tooltip_oss;
            tooltip_oss << "VBlank Sync Divisor (0-8). Controls frame pacing similar to VSync divisors:\n\n";
            tooltip_oss << "  0 -> No additional wait (Off)\n";
            for (int div = 1; div <= 8; ++div) {
                int effective_fps = static_cast<int>(std::round(refresh_hz / div));
                tooltip_oss << "  " << div << " -> " << effective_fps << " FPS";
                if (div == 1)
                    tooltip_oss << " (Full Refresh)";
                else if (div == 2)
                    tooltip_oss << " (Half Refresh)";
                else
                    tooltip_oss << " (1/" << div << " Refresh)";
                tooltip_oss << "\n";
            }
            tooltip_oss << "\n0 = Disabled, higher values reduce effective frame rate for smoother frame pacing.";
            ImGui::SetTooltip("%s", tooltip_oss.str().c_str());
        }

        // VBlank Monitor Status (only visible if latent sync is enabled and FPS limit > 0)
        if (s_fps_limiter_mode.load() == FpsLimiterMode::kLatentSync) {
            if (dxgi::latent_sync::g_latentSyncManager) {
                auto& latent = dxgi::latent_sync::g_latentSyncManager->GetLatentLimiter();
                if (latent.IsVBlankMonitoringActive()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ui::colors::STATUS_ACTIVE, "✁EVBlank Monitor: ACTIVE");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring thread is running and collecting scanline data for frame pacing.");
                    }

                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  refresh time: %.3fms",
                                       1.0 * dxgi::fps_limiter::ns_per_refresh.load() / utils::NS_TO_MS);
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  total_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_total_height.load());
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::STATUS_INACTIVE, "  active_height: %llu",
                                       dxgi::fps_limiter::g_latent_sync_active_height.load());
                } else {
                    ImGui::Spacing();
                    ImGui::TextColored(ui::colors::STATUS_STARTING, ICON_FK_WARNING " VBlank Monitor: STARTING...");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "VBlank monitoring is enabled but the monitoring thread is still starting up.");
                    }
                }
            }
        }
    }

    ImGui::Spacing();

    // FPS Limit slider (persisted)

    bool fps_limit_enabled = s_fps_limiter_mode.load() != FpsLimiterMode::kDisabled && s_fps_limiter_mode.load() != FpsLimiterMode::kLatentSync || settings::g_developerTabSettings.reflex_enable.GetValue();


    {
        if (!fps_limit_enabled) {
            ImGui::BeginDisabled();
        }

        float current_value = settings::g_mainTabSettings.fps_limit.GetValue();
        const char* fmt = (current_value > 0.0f) ? "%.3f FPS" : "No Limit";
        if (SliderFloatSettingRef(settings::g_mainTabSettings.fps_limit, "FPS Limit", fmt)) {}

        auto cur_limit = settings::g_mainTabSettings.fps_limit.GetValue();
        if (cur_limit > 0.0f && cur_limit < 10.0f) {
            settings::g_mainTabSettings.fps_limit.SetValue(0.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set FPS limit for the game (0 = no limit). Now uses the new Custom FPS Limiter system.");
        }

        if (!fps_limit_enabled) {
            ImGui::EndDisabled();
        }
    }

    // FPS Limiter Warning - Check if OnPresentFlags events are working
    if (fps_limit_enabled) {
        uint32_t event_count = g_reshade_event_counters[RESHADE_EVENT_PRESENT_FLAGS].load();
        bool show_warning = (event_count == 0);

        if (show_warning) {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_WARNING,
                ICON_FK_WARNING " Warning: FPS limiting is enabled but SWAPCHAIN_EVENT_PRESENT_FLAGS events are 0. "
                "FPS limiting may not work properly.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("SWAPCHAIN_EVENT_PRESENT_FLAGS events are required for FPS limiting.");
            }
        }
    }

    // No Render in Background checkbox
    {
        bool no_render_in_bg = settings::g_mainTabSettings.no_render_in_background.GetValue();
        if (ImGui::Checkbox("No Render in Background", &no_render_in_bg)) {
            settings::g_mainTabSettings.no_render_in_background.SetValue(no_render_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip rendering draw calls when the game window is not in the foreground. This can save "
                "GPU power and reduce background processing.");
        }
        ImGui::SameLine();
        bool no_present_in_bg = settings::g_mainTabSettings.no_present_in_background.GetValue();
        if (ImGui::Checkbox("No Present in Background", &no_present_in_bg)) {
            settings::g_mainTabSettings.no_present_in_background.SetValue(no_present_in_bg);
            // The setting is automatically synced via BoolSettingRef
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Skip ReShade's on_present processing when the game window is not in the foreground. "
                "This can save GPU power and reduce background processing.");
        }
    }

    // VSync & Tearing controls
    {
        // Main FPS Limit slider (persisted)
        {
            if (!fps_limit_enabled) {
                ImGui::BeginDisabled();
            }
            DrawQuickResolutionChanger();

            if (!fps_limit_enabled) {
                ImGui::EndDisabled();
            }
        }

        ImGui::Spacing();


        // Background FPS Limit slider (persisted)
        {
            if (!fps_limit_enabled) {
                ImGui::BeginDisabled();
            }

            float current_bg = settings::g_mainTabSettings.fps_limit_background.GetValue();
            const char* fmt_bg = (current_bg > 0.0f) ? "%.0f FPS" : "No Limit";
            if (SliderFloatSettingRef(settings::g_mainTabSettings.fps_limit_background, "Background FPS Limit", fmt_bg)) {}

            if (!fps_limit_enabled) {
                ImGui::EndDisabled();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "FPS cap when the game window is not in the foreground. Now uses the new Custom FPS Limiter system.");
            }
        }
        if (ImGui::CollapsingHeader("VSync & Tearing", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool vs_on = settings::g_mainTabSettings.force_vsync_on.GetValue();
            if (ImGui::Checkbox("Force VSync ON", &vs_on)) {
                s_restart_needed_vsync_tearing.store(true);
                // Mutual exclusion
                if (vs_on) {
                    settings::g_mainTabSettings.force_vsync_off.SetValue(false);
                    // s_force_vsync_off is automatically synced via BoolSettingRef
                }
                settings::g_mainTabSettings.force_vsync_on.SetValue(vs_on);
                // s_force_vsync_on is automatically synced via BoolSettingRef
                LogInfo(vs_on ? "Force VSync ON enabled" : "Force VSync ON disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Forces sync interval = 1 (requires restart).");
            }

            ImGui::SameLine();

            bool vs_off = settings::g_mainTabSettings.force_vsync_off.GetValue();
            if (ImGui::Checkbox("Force VSync OFF", &vs_off)) {
                s_restart_needed_vsync_tearing.store(true);
                // Mutual exclusion
                if (vs_off) {
                    settings::g_mainTabSettings.force_vsync_on.SetValue(false);
                }
                settings::g_mainTabSettings.force_vsync_off.SetValue(vs_off);
                // s_force_vsync_off is automatically synced via BoolSettingRef
                LogInfo(vs_off ? "Force VSync OFF enabled" : "Force VSync OFF disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Forces sync interval = 0 (requires restart).");
            }

            ImGui::SameLine();

            bool prevent_t = settings::g_mainTabSettings.prevent_tearing.GetValue();
            if (ImGui::Checkbox("Prevent Tearing", &prevent_t)) {
                settings::g_mainTabSettings.prevent_tearing.SetValue(prevent_t);
                // s_prevent_tearing is automatically synced via BoolSettingRef
                LogInfo(prevent_t ? "Prevent Tearing enabled (tearing flags will be cleared)" : "Prevent Tearing disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevents tearing by clearing DXGI tearing flags and preferring sync.");
            }

            ImGui::Spacing();
            bool native_fp = settings::g_mainTabSettings.native_frame_pacing.GetValue();
            if (ImGui::Checkbox("Native Frame Pacing", &native_fp)) {
                settings::g_mainTabSettings.native_frame_pacing.SetValue(native_fp);
                LogInfo(native_fp ? "Native Frame Pacing enabled" : "Native Frame Pacing disabled");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enables native swapchain frame pacing logic in DXGISwapChain4Wrapper.\n"
                                "When enabled, common present logic (frame statistics, refresh rate monitoring, etc.)\n"
                                "is executed in the wrapper for native swapchains, avoiding duplicate execution in detour functions.");
            }

            int current_api = g_last_reshade_device_api.load();
            bool is_d3d9 = current_api == static_cast<int>(reshade::api::device_api::d3d9);
            bool is_dxgi = g_last_reshade_device_api.load() == static_cast<int>(reshade::api::device_api::d3d10)
            || current_api == static_cast<int>(reshade::api::device_api::d3d11)
            || current_api == static_cast<int>(reshade::api::device_api::d3d12);
            bool enable_flip = settings::g_developerTabSettings.enable_flip_chain.GetValue();

            bool is_flip = g_last_swapchain_desc.load() && (g_last_swapchain_desc.load()->present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD
                || g_last_swapchain_desc.load()->present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL);
            static bool has_been_enabled = false;
            has_been_enabled |= is_dxgi && (enable_flip || !is_flip);

            if (has_been_enabled) {
                ImGui::SameLine();

                if (ImGui::Checkbox("Enable Flip Chain (requires restart)", &enable_flip)) {
                    settings::g_developerTabSettings.enable_flip_chain.SetValue(enable_flip);
                    s_enable_flip_chain.store(enable_flip);
                    s_restart_needed_vsync_tearing.store(true);
                    LogInfo(enable_flip ? "Enable Flip Chain enabled" : "Enable Flip Chain disabled");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
                                    "This setting requires a game restart to take effect.\n"
                                    "Only works with DirectX 10/11/12 (DXGI) games.");
                }
            }

            if (is_d3d9) {
                ImGui::SameLine();
                // TODO add checkbox enable D9EX with flip model
                bool enable_d9ex_with_flip = settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue();
                if (ImGui::Checkbox("Enable Flip State (requires restart)", &enable_d9ex_with_flip)) {
                    settings::g_experimentalTabSettings.d3d9_flipex_enabled.SetValue(enable_d9ex_with_flip);
                    LogInfo(enable_d9ex_with_flip ? "Enable D9EX with Flip Model enabled" : "Enable D9EX with Flip Model disabled");
                }
            }

            // Display restart-required notice if flagged
            if (s_restart_needed_vsync_tearing.load()) {
                ImGui::Spacing();
                ImGui::TextColored(ui::colors::TEXT_ERROR, "Game restart required to apply VSync/tearing changes.");
            }

            // Display current present mode information
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            auto desc_ptr = g_last_swapchain_desc.load();
            if (desc_ptr) {
                const auto& desc = *desc_ptr;

                // Present mode display with tooltip
                ImGui::TextColored(ui::colors::TEXT_LABEL, "Current Present Mode:");
                ImGui::SameLine();
                ImVec4 present_mode_color = ui::colors::TEXT_DIMMED;
                // DXGI specific display
                // Determine present mode name and color
                std::string present_mode_name = "Unknown";

                if (is_d3d9) {
                    // dx9 device
                    #if 0
                    IDirect3DDevice9* d3d9_device = nullptr;
                    IDirect3DSwapChain9* swap_chain = nullptr;
                    HRESULT hr = d3d9_device->GetSwapChain(0, &swap_chain);

                    bool is_fullscreen = false;
                    if (SUCCEEDED(hr) && swap_chain) {
                        D3DPRESENT_PARAMETERS params;
                        hr = swap_chain->GetPresentParameters(&params);
                        if (SUCCEEDED(hr)) {
                            is_fullscreen = !params.Windowed;
                        }
                        swap_chain->Release();
                    }
                    if (d3d9_device != nullptr) {
                        d3d9_device->Release();
                    }
                    #endif

                    if (desc.present_mode == D3DSWAPEFFECT_FLIPEX) {
                        present_mode_name = "FLIPEX (Flip Model)";
                        present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                    } else if (desc.present_mode == D3DSWAPEFFECT_DISCARD) {
                        present_mode_name = "DISCARD (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else if (desc.present_mode == D3DSWAPEFFECT_FLIP) {
                        present_mode_name = "FLIP (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else if (desc.present_mode == D3DSWAPEFFECT_COPY) {
                        present_mode_name = "COPY (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else if (desc.present_mode == D3DSWAPEFFECT_OVERLAY)  {
                        present_mode_name = "OVERLAY (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else {
                        present_mode_name = "Unknown";
                        present_mode_color = ui::colors::TEXT_ERROR;
                    }
                    if (desc.fullscreen_state) {
                        present_mode_name = present_mode_name + "(FSE)";
                    }
                    DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

                    ImVec4 flip_color;
                    if (flip_state == DxgiBypassMode::kComposed) {
                        flip_color = ui::colors::FLIP_COMPOSED; // Red - bad
                    } else if (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip) {
                        flip_color = ui::colors::FLIP_INDEPENDENT; // Green - good
                    } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull ||
                               flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1 ||
                               flip_state == DxgiBypassMode::kQueryFailedNoMedia ||
                               flip_state == DxgiBypassMode::kQueryFailedNoStats) {
                        flip_color = ui::colors::TEXT_ERROR; // Red - query failed
                    } else {
                        flip_color = ui::colors::FLIP_UNKNOWN; // Yellow - unknown/unset
                    }
                    const char* flip_state_str = "Unknown";
                    switch (flip_state) {
                        case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
                        case DxgiBypassMode::kComposed:                 flip_state_str = "Composed"; break;
                        case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO iFlip"; break;
                        case DxgiBypassMode::kIndependentFlip:          flip_state_str = "iFlip"; break;
                        case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Null"; break;
                        case DxgiBypassMode::kQueryFailedNoMedia:       flip_state_str = "Query Failed: No Media"; break;
                        case DxgiBypassMode::kQueryFailedNoSwapchain1:  flip_state_str = "Query Failed: No Swapchain1"; break;
                        case DxgiBypassMode::kQueryFailedNoStats:       flip_state_str = "Query Failed: No Stats"; break;
                        case DxgiBypassMode::kUnknown:
                        default:                                        {
                            if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
                                flip_state_str = "Unknown";
                            } else {
                                flip_state_str = "Unavailable";
                            }
                            break;
                        }
                    }
                    ImGui::TextColored(present_mode_color, "%s", present_mode_name.c_str());

                    // Add DxgiBypassMode on the same line
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
                    ImGui::SameLine();
                    ImGui::TextColored(flip_color, "Status: %s", flip_state_str);

                    // Check for Discord Overlay and show warning
                    static DWORD last_discord_check = 0;
                    DWORD current_time = GetTickCount();
                    static bool discord_overlay_visible = false;

                    if (current_time - last_discord_check > 1000) {  // Check every second
                        discord_overlay_visible = display_commander::utils::IsWindowWithTitleVisible(L"Discord Overlay");
                        last_discord_check = current_time;
                    }

                    if (discord_overlay_visible) {
                        ImGui::SameLine();
                        ui::colors::PushIconColor(ui::colors::ICON_WARNING);
                        ImGui::Text(ICON_FK_WARNING " Discord Overlay");
                        ui::colors::PopIconColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Discord Overlay is visible and may prevent MPO iFlip.\n"
                                            "It can prevent Independent Flip mode and increase latency.\n"
                                            "Consider disabling it or setting AllowWindowedMode=true in Special-K.");
                        }
                    }

                } else if (is_dxgi) {

                    if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD) {
                        present_mode_name = "FLIP_DISCARD (Flip Model)";
                        present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                    } else if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
                        present_mode_name = "FLIP_SEQUENTIAL (Flip Model)";
                        present_mode_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
                    } else if (desc.present_mode == DXGI_SWAP_EFFECT_DISCARD) {
                        present_mode_name = "DISCARD (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else if (desc.present_mode == DXGI_SWAP_EFFECT_SEQUENTIAL) {
                        present_mode_name = "SEQUENTIAL (Traditional)";
                        present_mode_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange
                    } else {
                        present_mode_name = "Unknown";
                        present_mode_color = ui::colors::TEXT_ERROR;
                    }
                    ImGui::TextColored(present_mode_color, "%s", present_mode_name.c_str());

                    // Add DxgiBypassMode on the same line
                    ImGui::SameLine();
                    ImGui::TextColored(ui::colors::TEXT_DIMMED, " | ");
                    ImGui::SameLine();

                    // Get flip state information
                    int current_api = g_last_reshade_device_api.load();
                    DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

                    const char* flip_state_str = "Unknown";
                    switch (flip_state) {
                        case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
                        case DxgiBypassMode::kComposed:                 flip_state_str = "Composed"; break;
                        case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO iFlip"; break;
                        case DxgiBypassMode::kIndependentFlip:          flip_state_str = "iFlip"; break;
                        case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Null"; break;
                        case DxgiBypassMode::kQueryFailedNoMedia:       {
                            if (GetModuleHandleA("sl.interposer.dll") != nullptr) {
                                flip_state_str = "(Streamline Interposer detected - Flip State Query not supported)";
                            } else {
                                flip_state_str = "Query Failed: No Media";
                            }
                            break;
                        }
                        case DxgiBypassMode::kQueryFailedNoSwapchain1:  flip_state_str = "Query Failed: No Swapchain1"; break;
                        case DxgiBypassMode::kQueryFailedNoStats:       flip_state_str = "Query Failed: No Stats"; break;
                        case DxgiBypassMode::kUnknown:
                        default:                                        {
                            if (desc.present_mode == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.present_mode == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) {
                                flip_state_str = "Unknown";
                            } else {
                                flip_state_str = "Unavailable";
                            }
                            break;
                        }
                    }

                    ImVec4 flip_color;
                    if (flip_state == DxgiBypassMode::kComposed) {
                        flip_color = ui::colors::FLIP_COMPOSED; // Red - bad
                    } else if (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip) {
                        flip_color = ui::colors::FLIP_INDEPENDENT; // Green - good
                    } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull ||
                               flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1 ||
                               flip_state == DxgiBypassMode::kQueryFailedNoMedia ||
                               flip_state == DxgiBypassMode::kQueryFailedNoStats) {
                        flip_color = ui::colors::TEXT_ERROR; // Red - query failed
                    } else {
                        flip_color = ui::colors::FLIP_UNKNOWN; // Yellow - unknown/unset
                    }

                    ImGui::TextColored(flip_color, "Status: %s", flip_state_str);

                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::TextColored(ui::colors::TEXT_LABEL, "Swapchain Information:");
                        ImGui::Separator();

                        // Present mode details
                        ImGui::Text("Present Mode: %s", present_mode_name.c_str());
                        ImGui::Text("Present Mode ID: %u", desc.present_mode);

                        // Flip state information
                        ImGui::Text("Status: %s", flip_state_str);

                        // Window information for debugging flip composed state
                        HWND game_window = display_commanderhooks::GetGameWindow();
                        if (game_window != nullptr && IsWindow(game_window)) {
                            ImGui::Separator();
                            ImGui::TextColored(ui::colors::TEXT_LABEL, "Window Information (Debug):");

                            // Window coordinates
                            RECT window_rect = {};
                            RECT client_rect = {};
                            if (GetWindowRect(game_window, &window_rect) && GetClientRect(game_window, &client_rect)) {
                                ImGui::Text("Window Rect: (%ld, %ld) to (%ld, %ld)",
                                          window_rect.left, window_rect.top,
                                          window_rect.right, window_rect.bottom);
                                ImGui::Text("Window Size: %ld x %ld",
                                          window_rect.right - window_rect.left,
                                          window_rect.bottom - window_rect.top);
                                ImGui::Text("Client Rect: (%ld, %ld) to (%ld, %ld)",
                                          client_rect.left, client_rect.top,
                                          client_rect.right, client_rect.bottom);
                                ImGui::Text("Client Size: %ld x %ld",
                                          client_rect.right - client_rect.left,
                                          client_rect.bottom - client_rect.top);
                            }

                            // Window flags
                            LONG_PTR style = GetWindowLongPtrW(game_window, GWL_STYLE);
                            LONG_PTR ex_style = GetWindowLongPtrW(game_window, GWL_EXSTYLE);

                            ImGui::Text("Window Style: 0x%08lX", static_cast<unsigned long>(style));
                            ImGui::Text("Window ExStyle: 0x%08lX", static_cast<unsigned long>(ex_style));

                            // Key style flags that affect flip mode
                            bool is_popup = (style & WS_POPUP) != 0;
                            bool is_child = (style & WS_CHILD) != 0;
                            bool has_caption = (style & WS_CAPTION) != 0;
                            bool has_border = (style & WS_BORDER) != 0;
                            bool is_layered = (ex_style & WS_EX_LAYERED) != 0;
                            bool is_topmost = (ex_style & WS_EX_TOPMOST) != 0;
                            bool is_transparent = (ex_style & WS_EX_TRANSPARENT) != 0;

                            ImGui::Text("  WS_POPUP: %s", is_popup ? "Yes" : "No");
                            ImGui::Text("  WS_CHILD: %s", is_child ? "Yes" : "No");
                            ImGui::Text("  WS_CAPTION: %s", has_caption ? "Yes" : "No");
                            ImGui::Text("  WS_BORDER: %s", has_border ? "Yes" : "No");
                            ImGui::Text("  WS_EX_LAYERED: %s", is_layered ? "Yes" : "No");
                            ImGui::Text("  WS_EX_TOPMOST: %s", is_topmost ? "Yes" : "No");
                            ImGui::Text("  WS_EX_TRANSPARENT: %s", is_transparent ? "Yes" : "No");

                            // Backbuffer vs window size comparison
                            ImGui::Separator();
                            ImGui::TextColored(ui::colors::TEXT_LABEL, "Size Comparison:");
                            ImGui::Text("Backbuffer: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);
                            if (GetWindowRect(game_window, &window_rect)) {
                                long window_width = window_rect.right - window_rect.left;
                                long window_height = window_rect.bottom - window_rect.top;
                                ImGui::Text("Window: %ldx%ld", window_width, window_height);

                                bool size_matches = (static_cast<long>(desc.back_buffer.texture.width) == window_width &&
                                                   static_cast<long>(desc.back_buffer.texture.height) == window_height);
                                if (size_matches) {
                                    ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Sizes match");
                                } else {
                                    ImGui::TextColored(ui::colors::TEXT_WARNING, "  Sizes differ (may cause Composed Flip)");
                                }
                            }

                            // Display/monitor information
                            ImGui::Separator();
                            ImGui::TextColored(ui::colors::TEXT_LABEL, "Display Information:");
                            HMONITOR monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);
                            if (monitor != nullptr) {
                                MONITORINFOEXW monitor_info = {};
                                monitor_info.cbSize = sizeof(MONITORINFOEXW);
                                if (GetMonitorInfoW(monitor, &monitor_info)) {
                                    ImGui::Text("Monitor Rect: (%ld, %ld) to (%ld, %ld)",
                                              monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                                              monitor_info.rcMonitor.right, monitor_info.rcMonitor.bottom);
                                    long monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                                    long monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
                                    ImGui::Text("Monitor Size: %ld x %ld", monitor_width, monitor_height);

                                    // Check if window covers entire monitor
                                    if (GetWindowRect(game_window, &window_rect)) {
                                        bool covers_monitor = (window_rect.left == monitor_info.rcMonitor.left &&
                                                             window_rect.top == monitor_info.rcMonitor.top &&
                                                             window_rect.right == monitor_info.rcMonitor.right &&
                                                             window_rect.bottom == monitor_info.rcMonitor.bottom);
                                        if (covers_monitor) {
                                            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "  Window covers entire monitor");
                                        } else {
                                            ImGui::TextColored(ui::colors::TEXT_WARNING, "  Window does not cover entire monitor");
                                        }
                                    }
                                }
                            }
                        }

                        // Detailed explanation for Composed Flip
                        if (flip_state == DxgiBypassMode::kComposed) {
                            ImGui::Separator();
                            ImGui::TextColored(ui::colors::FLIP_COMPOSED, "  - Composed Flip (Red): Desktop Window Manager composition mode");
                            ImGui::Text("    Higher latency, not ideal for gaming");
                            ImGui::Spacing();
                            ImGui::TextColored(ui::colors::TEXT_LABEL, "Why Composed Flip?");
                            ImGui::BulletText("Fullscreen: No (Borderless windowed mode)");
                            ImGui::BulletText("DWM composition required for windowed mode");
                            ImGui::BulletText("Independent Flip requires True Fullscreen Exclusive (FSE)");
                            ImGui::Spacing();
                            ImGui::TextColored(ui::colors::TEXT_DIMMED, "To achieve Independent Flip:");
                            ImGui::BulletText("Enable True Fullscreen Exclusive in game settings");
                            ImGui::BulletText("Or use borderless fullscreen with exact resolution match");
                            ImGui::BulletText("Ensure no overlays or DWM effects are active");
                        } else if (flip_state == DxgiBypassMode::kOverlay) {
                            ImGui::TextColored(ui::colors::FLIP_INDEPENDENT, "  - MPO Independent Flip (Green): Modern hardware overlay plane");
                            ImGui::Text("    Best performance and lowest latency");
                        } else if (flip_state == DxgiBypassMode::kIndependentFlip) {
                            ImGui::TextColored(ui::colors::FLIP_INDEPENDENT, "  - Independent Flip (Green): Legacy direct flip mode");
                            ImGui::Text("    Good performance and low latency");
                        } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull) {
                            ImGui::TextColored(ui::colors::TEXT_ERROR, "  - Query Failed: Swapchain is null");
                            ImGui::Text("    Cannot determine flip state - swapchain not available");
                        } else if (flip_state == DxgiBypassMode::kQueryFailedNoMedia) {
                            if (GetModuleHandleA("sl.interposer.dll") != nullptr) {
                                ImGui::TextColored(ui::colors::TEXT_ERROR,  ICON_FK_WARNING "  - Streamline Interposer detected - Flip State Query not supported");
                                ImGui::Text("    Cannot determine flip state - call after at least one Present");
                            } else {
                                ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: GetFrameStatisticsMedia failed");
                                ImGui::Text("    Cannot determine flip state - call after at least one Present");
                            }
                        } else if (flip_state == DxgiBypassMode::kQueryFailedNoStats) {
                            ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: GetFrameStatisticsMedia failed");
                            ImGui::Text("    Cannot determine flip state - call after at least one Present");
                        } else if (flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1) {
                            ImGui::TextColored(ui::colors::TEXT_ERROR, "  • Query Failed: IDXGISwapChain1 not available");
                            ImGui::Text("    Cannot determine flip state - SwapChain1 interface not supported");
                        } else if (flip_state == DxgiBypassMode::kUnset) {
                            ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "  • Flip state not yet queried");
                            ImGui::Text("    Initial state - will be determined on first query");
                        } else {
                            ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "  • Flip state not yet determined");
                            ImGui::Text("    Wait for a few frames to render");
                        }

                        // Back buffer information
                        ImGui::Text("Back Buffer Count: %u", desc.back_buffer_count);
                        ImGui::Text("Back Buffer Size: %ux%u", desc.back_buffer.texture.width, desc.back_buffer.texture.height);

                        // Format information
                        const char* format_name = "Unknown";
                        switch (desc.back_buffer.texture.format) {
                            case reshade::api::format::r10g10b10a2_unorm: format_name = "R10G10B10A2_UNORM (HDR 10-bit)"; break;
                            case reshade::api::format::r16g16b16a16_float: format_name = "R16G16B16A16_FLOAT (HDR 16-bit)"; break;
                            case reshade::api::format::r8g8b8a8_unorm: format_name = "R8G8B8A8_UNORM (SDR 8-bit)"; break;
                            case reshade::api::format::b8g8r8a8_unorm: format_name = "B8G8R8A8_UNORM (SDR 8-bit)"; break;
                            default: format_name = "Unknown Format"; break;
                        }
                        ImGui::Text("Back Buffer Format: %s", format_name);

                        // Sync and fullscreen information
                        ImGui::Text("Sync Interval: %u", desc.sync_interval);
                        ImGui::Text("Fullscreen: %s", desc.fullscreen_state ? "Yes" : "No");
                        if (desc.fullscreen_state && desc.fullscreen_refresh_rate > 0) {
                            ImGui::Text("Refresh Rate: %.2f Hz", desc.fullscreen_refresh_rate);
                        }

                        // Present flags (for DXGI)
                        if (desc.present_flags != 0) {
                            ImGui::Text("Device Creation Flags: 0x%X", desc.present_flags);

                            // Decode common present flags
                            ImGui::Text("Flags:");
                            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
                                ImGui::Text("  • ALLOW_TEARING (VRR/G-Sync)");
                            }
                            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
                                ImGui::Text("  • FRAME_LATENCY_WAITABLE_OBJECT");
                            }
                            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY) {
                                ImGui::Text("  • DISPLAY_ONLY");
                            }
                            if (desc.present_flags & DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT) {
                                ImGui::Text("  • RESTRICTED_CONTENT");
                            }
                        }

                        ImGui::EndTooltip();
                    }
                } else {
                    present_mode_name = "Unsupported API (WIP)";
                    present_mode_color = ui::colors::TEXT_ERROR;
                }

            } else {
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "No swapchain information available");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("No game detected or swapchain not yet created.\nThis information will appear once a game is running.");
                }
            }
        }
    }

}

void DrawAudioSettings() {
    // Audio Volume slider
    float volume = s_audio_volume_percent.load();
    if (ImGui::SliderFloat("Game Volume (%)", &volume, 0.0f, 100.0f, "%.0f%%")) {
        s_audio_volume_percent.store(volume);

        // Apply immediately only if Auto-apply is enabled
        if (settings::g_mainTabSettings.audio_volume_auto_apply.GetValue()) {
            if (::SetVolumeForCurrentProcess(volume)) {
                std::ostringstream oss;
                oss << "Game volume changed to " << static_cast<int>(volume) << "%";
                LogInfo(oss.str().c_str());
            } else {
                std::ostringstream oss;
                oss << "Failed to set game volume to " << static_cast<int>(volume) << "%";
                LogWarn(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Game audio volume control (0-100%%). When at 100%%, volume adjustments will affect system volume instead.");
    }
    // Auto-apply checkbox next to Audio Volume
    ImGui::SameLine();
    if (CheckboxSetting(settings::g_mainTabSettings.audio_volume_auto_apply, "Auto-apply##audio_volume")) {
        // No immediate action required; stored for consistency with other UI
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Auto-apply volume changes when adjusting the slider.");
    }

    // System Volume slider (controls system master volume directly)
    float system_volume = 0.0f;
    if (::GetSystemVolume(&system_volume)) {
        s_system_volume_percent.store(system_volume);
    } else {
        system_volume = s_system_volume_percent.load();
    }
    if (ImGui::SliderFloat("System Volume (%)", &system_volume, 0.0f, 100.0f, "%.0f%%")) {
        s_system_volume_percent.store(system_volume);
        if (!::SetSystemVolume(system_volume)) {
            std::ostringstream oss;
            oss << "Failed to set system volume to " << static_cast<int>(system_volume) << "%";
            LogWarn(oss.str().c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "System master volume control (0-100%%). This adjusts the Windows system volume for the default output "
            "device.\n"
            "Note: System volume may also be adjusted automatically when game volume is at 100%% and you increase it.");
    }

    // Audio Mute checkbox
    bool audio_mute = s_audio_mute.load();
    if (ImGui::Checkbox("Mute", &audio_mute)) {
        settings::g_mainTabSettings.audio_mute.SetValue(audio_mute);

        // Apply mute/unmute immediately
        if (::SetMuteForCurrentProcess(audio_mute)) {
            ::g_muted_applied.store(audio_mute);
            std::ostringstream oss;
            oss << "Audio " << (audio_mute ? "muted" : "unmuted") << " successfully";
            LogInfo(oss.str().c_str());
        } else {
            std::ostringstream oss;
            oss << "Failed to " << (audio_mute ? "mute" : "unmute") << " audio";
            LogWarn(oss.str().c_str());
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Manually mute/unmute audio.");
    }


    // Mute in Background checkbox (disabled if Mute is ON)
    bool mute_in_bg = s_mute_in_background.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background", &mute_in_bg)) {
        settings::g_mainTabSettings.mute_in_background.SetValue(mute_in_bg);
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(false);

        // Reset applied flag so the monitor thread reapplies desired state
        ::g_muted_applied.store(false);
        // Also apply the current mute state immediately if manual mute is off
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            // Use actual focus state for background audio (not spoofed)
            bool want_mute = (mute_in_bg && hwnd != nullptr && GetForegroundWindow() != hwnd);
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute " << (mute_in_bg ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute the game's audio when it is not the foreground window.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }
    // Mute in Background only if other app plays audio
    bool mute_in_bg_if_other = s_mute_in_background_if_other_audio.load();
    if (s_audio_mute.load()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Mute In Background (only if other app has audio)", &mute_in_bg_if_other)) {
        settings::g_mainTabSettings.mute_in_background_if_other_audio.SetValue(mute_in_bg_if_other);
        settings::g_mainTabSettings.mute_in_background.SetValue(false);
        ::g_muted_applied.store(false);
        if (!s_audio_mute.load()) {
            HWND hwnd = g_last_swapchain_hwnd.load();
            bool is_background = (hwnd != nullptr && GetForegroundWindow() != hwnd);
            bool want_mute = (mute_in_bg_if_other && is_background && ::IsOtherAppPlayingAudio());
            if (::SetMuteForCurrentProcess(want_mute)) {
                ::g_muted_applied.store(want_mute);
                std::ostringstream oss;
                oss << "Background mute (if other audio) " << (mute_in_bg_if_other ? "enabled" : "disabled");
                LogInfo(oss.str().c_str());
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mute only if app is background AND another app outputs audio.");
    }
    if (s_audio_mute.load()) {
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    // Audio output device selector (per-application routing)
    ImGui::Text("Output Device");

    static std::vector<std::string> s_audio_device_names;
    static std::vector<std::wstring> s_audio_device_ids;
    static int s_selected_audio_device_index = 0; // 0 = System Default, 1..N = devices
    static bool s_audio_devices_initialized = false;

    // Refresh device list on first use or when user clicks refresh
    auto refresh_audio_devices = []() {
        s_audio_device_names.clear();
        s_audio_device_ids.clear();
        s_selected_audio_device_index = 0;

        std::wstring current_device_id;
        if (GetAudioOutputDevices(s_audio_device_names, s_audio_device_ids, current_device_id)) {
            // Determine current selection: empty = system default, otherwise match by short device id
            if (current_device_id.empty()) {
                s_selected_audio_device_index = 0;
            } else {
                int matched = 0;
                for (size_t i = 0; i < s_audio_device_ids.size(); ++i) {
                    if (s_audio_device_ids[i] == current_device_id) {
                        matched = static_cast<int>(i) + 1;
                        break;
                    }
                }
                s_selected_audio_device_index = matched;
            }
        }
    };

    if (!s_audio_devices_initialized) {
        refresh_audio_devices();
        s_audio_devices_initialized = true;
    }

    const char *current_label = "System Default";
    if (s_selected_audio_device_index > 0 &&
        static_cast<size_t>(s_selected_audio_device_index - 1) < s_audio_device_names.size()) {
        current_label = s_audio_device_names[s_selected_audio_device_index - 1].c_str();
    }

    if (ImGui::BeginCombo("##AudioOutputDevice", current_label)) {
        bool selection_changed = false;

        // Option 0: System Default (no per-app override)
        bool selected_default = (s_selected_audio_device_index == 0);
        if (ImGui::Selectable("System Default (use Windows setting)", selected_default)) {
            if (SetAudioOutputDeviceForCurrentProcess(L"")) {
                s_selected_audio_device_index = 0;
                selection_changed = true;
            }
        }
        if (selected_default) {
            ImGui::SetItemDefaultFocus();
        }

        // Actual devices
        for (int i = 0; i < static_cast<int>(s_audio_device_names.size()); ++i) {
            bool selected = (s_selected_audio_device_index == i + 1);
            if (ImGui::Selectable(s_audio_device_names[i].c_str(), selected)) {
                if (i >= 0 && static_cast<size_t>(i) < s_audio_device_ids.size()) {
                    if (SetAudioOutputDeviceForCurrentProcess(s_audio_device_ids[i])) {
                        s_selected_audio_device_index = i + 1;
                        selection_changed = true;
                    }
                }
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();

        if (selection_changed) {
            // No additional state to sync; Windows persists per-process routing itself.
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Select which audio output device this game should use.\n"
            "Uses Windows per-application audio routing (similar to 'App volume and device preferences').");
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh Devices")) {
        refresh_audio_devices();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Re-scan active audio output devices (use after plugging/unplugging audio hardware).");
    }

}

void DrawWindowControls() {
    HWND hwnd = g_last_swapchain_hwnd.load();
    if (hwnd == nullptr) {
        LogWarn("Maximize Window: no window handle available");
        return;
    }
    // Window Control Buttons (Minimize, Restore, and Maximize side by side)
    ImGui::BeginGroup();

    // Minimize Window Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_MINUS " Minimize Window")) {
        HWND hwnd = g_last_swapchain_hwnd.load();
        std::thread([hwnd]() {
            LogDebug("Minimize Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_MINIMIZE);
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Minimize the current game window.");
    }

    ImGui::SameLine();

    // Open Game Folder Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_FOLDER_OPEN " Open Game Folder")) {
        std::thread([]() {
            LogDebug("Open Game Folder button pressed (bg thread)");

            // Get current process executable path
            char process_path[MAX_PATH];
            DWORD path_length = GetModuleFileNameA(nullptr, process_path, MAX_PATH);

            if (path_length == 0) {
                LogError("Failed to get current process path for folder opening");
                return;
            }

            // Get the parent directory of the executable
            std::string full_path(process_path);
            size_t last_slash = full_path.find_last_of("\\/");

            if (last_slash == std::string::npos) {
                LogError("Invalid process path format: %s", full_path.c_str());
                return;
            }

            std::string game_folder = full_path.substr(0, last_slash);
            LogInfo("Opening game folder: %s", game_folder.c_str());

            // Open the folder in Windows Explorer
            HINSTANCE result = ShellExecuteA(nullptr, "explore", game_folder.c_str(), nullptr, nullptr, SW_SHOW);

            if (reinterpret_cast<intptr_t>(result) <= 32) {
                LogError("Failed to open game folder: %s (Error: %ld)", game_folder.c_str(), reinterpret_cast<intptr_t>(result));
            } else {
                LogInfo("Successfully opened game folder: %s", game_folder.c_str());
            }
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open the game's installation folder in Windows Explorer.");
    }

    ImGui::SameLine();

    // Restore Window Button
    ui::colors::PushIconColor(ui::colors::ICON_ACTION);
    if (ImGui::Button(ICON_FK_UNDO " Restore Window")) {
        std::thread([hwnd]() {
            LogDebug("Restore Window button pressed (bg thread)");
            ShowWindow(hwnd, SW_RESTORE);
            display_commanderhooks::SendFakeActivationMessages(hwnd);
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Restore the minimized game window.");
    }
    #if 0

    ImGui::SameLine();
    // Maximize Window Button
    ui::colors::PushIconColor(ui::colors::ICON_POSITIVE);
    if (ImGui::Button(ICON_FK_PLUS " Maximize Window")) {
        std::thread([hwnd]() {
            LogDebug("Maximize Window button pressed (bg thread)");

            // Switch to fullscreen mode to maximize the window
            s_window_mode.store(WindowMode::kFullscreen);
            settings::g_mainTabSettings.window_mode.SetValue(static_cast<int>(WindowMode::kFullscreen));

            // Apply the window changes using the existing UI system
            ApplyWindowChange(hwnd, "maximize_button");

            LogInfo("Window maximized to current monitor size");
        }).detach();
    }
    ui::colors::PopIconColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Maximize the window to fill the current monitor.");
    }
#endif
    ImGui::EndGroup();
}

void DrawImportantInfo() {


    // Test Overlay Control
    {
        bool show_test_overlay = settings::g_mainTabSettings.show_test_overlay.GetValue();
        if (ImGui::Checkbox(ICON_FK_SEARCH " Show Overlay", &show_test_overlay)) {
            settings::g_mainTabSettings.show_test_overlay.SetValue(show_test_overlay);
            LogInfo("Performance overlay %s", show_test_overlay ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Shows a performance monitoring widget in the main ReShade overlay with frame time graph, "
                "FPS counter, and other performance metrics. Demonstrates reshade_overlay event usage.");
        }

        // Grid layout for overlay display checkboxes (4 columns)
        ImGui::Columns(4, "overlay_checkboxes", false);

        // Show Playtime Control
        bool show_playtime = settings::g_mainTabSettings.show_playtime.GetValue();
        if (ImGui::Checkbox("Playtime", &show_playtime)) {
            settings::g_mainTabSettings.show_playtime.SetValue(show_playtime);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows total playtime (time from game start) in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show FPS Counter
        bool show_fps_counter = settings::g_mainTabSettings.show_fps_counter.GetValue();
        if (ImGui::Checkbox("FPS Counter", &show_fps_counter)) {
            settings::g_mainTabSettings.show_fps_counter.SetValue(show_fps_counter);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current FPS counter in the main ReShade overlay.");
        }
        ImGui::NextColumn();

        // Show Native FPS
        bool show_native_fps = settings::g_mainTabSettings.show_native_fps.GetValue();
        if (ImGui::Checkbox("Native FPS", &show_native_fps)) {
            settings::g_mainTabSettings.show_native_fps.SetValue(show_native_fps);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows native FPS (calculated from native Reflex sleep calls) alongside regular FPS in format: XX.X / YY.Y fps");
        }
        ImGui::NextColumn();

        // Show Refresh Rate
        bool show_refresh_rate = settings::g_mainTabSettings.show_refresh_rate.GetValue();
        if (ImGui::Checkbox("Refresh Rate", &show_refresh_rate)) {
            settings::g_mainTabSettings.show_refresh_rate.SetValue(show_refresh_rate);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current display refresh rate in the main ReShade overlay.");
        }
        ImGui::NextColumn();

        // Show VRR Status
        bool show_vrr_status = settings::g_mainTabSettings.show_vrr_status.GetValue();
        if (ImGui::Checkbox("VRR Status", &show_vrr_status)) {
            settings::g_mainTabSettings.show_vrr_status.SetValue(show_vrr_status);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows whether Variable Refresh Rate (VRR) is active in the performance overlay.");
        }
        ImGui::NextColumn();

        // VRR Debug Mode
        bool vrr_debug_mode = settings::g_mainTabSettings.vrr_debug_mode.GetValue();
        if (ImGui::Checkbox("VRR Debug Mode", &vrr_debug_mode)) {
            settings::g_mainTabSettings.vrr_debug_mode.SetValue(vrr_debug_mode);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows detailed VRR debugging parameters (Fixed Hz, Threshold, Samples, etc.) in the performance overlay.");
        }
        ImGui::NextColumn();

        // Show CPU Usage
        bool show_cpu_usage = settings::g_mainTabSettings.show_cpu_usage.GetValue();
        if (ImGui::Checkbox("CPU Usage", &show_cpu_usage)) {
            settings::g_mainTabSettings.show_cpu_usage.SetValue(show_cpu_usage);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows CPU usage as a percentage: (sim duration / frame time) * 100%");
        }
        ImGui::NextColumn();

        // Show Stopwatch Control
        bool show_stopwatch = settings::g_mainTabSettings.show_stopwatch.GetValue();
        if (ImGui::Checkbox("Stopwatch", &show_stopwatch)) {
            settings::g_mainTabSettings.show_stopwatch.SetValue(show_stopwatch);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows a stopwatch in the performance overlay. Use Ctrl+S to start/reset.");
        }
        ImGui::NextColumn();

        // GPU Measurement Enable/Disable Control
        bool gpu_measurement = settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0;
        if (ImGui::Checkbox("Show latency", &gpu_measurement)) {
            settings::g_mainTabSettings.gpu_measurement_enabled.SetValue(gpu_measurement ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Measures time from Present call to GPU completion using fences.\n"
                "Requires D3D11 with Windows 10+ or D3D12.\n"
                "Shows as 'GPU Duration' in the timing metrics below.");
        }
        ImGui::NextColumn();

        // Show Labels Control
        bool show_labels = settings::g_mainTabSettings.show_labels.GetValue();
        if (ImGui::Checkbox("Show labels", &show_labels)) {
            settings::g_mainTabSettings.show_labels.SetValue(show_labels);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows text labels (like 'fps:', 'lat:', etc.) before values in the overlay.");
        }
        ImGui::NextColumn();

        // Show Clock Control
        bool show_clock = settings::g_mainTabSettings.show_clock.GetValue();
        if (ImGui::Checkbox("Show clock", &show_clock)) {
            settings::g_mainTabSettings.show_clock.SetValue(show_clock);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current time (HH:MM:SS) in the overlay.");
        }
        ImGui::NextColumn();

        // Show Frame Time Graph Control
        bool show_frame_time_graph = settings::g_mainTabSettings.show_frame_time_graph.GetValue();
        if (ImGui::Checkbox("Show frame time graph", &show_frame_time_graph)) {
            settings::g_mainTabSettings.show_frame_time_graph.SetValue(show_frame_time_graph);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows a graph of frame times in the overlay.");
        }
        ImGui::NextColumn();

        // Show Refresh Rate Frame Times Graph Control
        bool show_refresh_rate_frame_times = settings::g_mainTabSettings.show_refresh_rate_frame_times.GetValue();
        if (ImGui::Checkbox("Show refresh rate frame times", &show_refresh_rate_frame_times)) {
            settings::g_mainTabSettings.show_refresh_rate_frame_times.SetValue(show_refresh_rate_frame_times);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows a graph of refresh rate frame times (display refresh intervals) in the overlay.");
        }
        ImGui::NextColumn();

        // Show Volume Control
        bool show_volume = settings::g_mainTabSettings.show_volume.GetValue();
        if (ImGui::Checkbox("Show volume", &show_volume)) {
            settings::g_mainTabSettings.show_volume.SetValue(show_volume);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shows the current audio volume percentage in the overlay.");
        }

        ImGui::Columns(1); // Reset to single column

        ImGui::Spacing();
        // Overlay background transparency slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_background_alpha, "Overlay Background Transparency", "%.2f")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls the transparency of the overlay background. 0.0 = fully transparent, 1.0 = fully opaque.");
        }
        // Overlay chart transparency slider
        if (SliderFloatSetting(settings::g_mainTabSettings.overlay_chart_alpha, "Frame Chart Transparency", "%.2f")) {
            // Setting is automatically saved by SliderFloatSetting
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Controls the transparency of the frame time and refresh rate chart backgrounds. 0.0 = fully transparent, 1.0 = fully opaque. Chart lines remain fully visible.");
        }

    }

    ImGui::Spacing();

    // FPS Counter with 1% Low and 0.1% Low over past 60s (computed in background)
    {
        std::string local_text;
        auto shared_text = ::g_perf_text_shared.load();
        if (shared_text) {
            local_text = *shared_text;
        }
        ImGui::TextUnformatted(local_text.c_str());
        ui::colors::PushIconColor(ui::colors::ICON_ACTION);
        if (ImGui::Button(ICON_FK_REFRESH " Reset Stats")) {
            ::g_perf_reset_requested.store(true, std::memory_order_release);
        }
        ui::colors::PopIconColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset FPS/frametime statistics. Metrics are computed since reset.");
        }
    }

    ImGui::Spacing();

    // Frame Time Graph Section (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 1: Nested subsection with indentation and distinct colors
    ImGui::Indent();  // Indent nested header
    ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
    if (ImGui::CollapsingHeader("Frame Time Graph", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();  // Indent content inside subsection
        // Display GPU fence status/failure reason
        if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0) {
            const char* failure_reason = ::g_gpu_fence_failure_reason.load();
            if (failure_reason != nullptr) {
                ImGui::Indent();
                ui::colors::PushIconColor(ui::colors::ICON_ERROR);
                ImGui::TextUnformatted(ICON_FK_WARNING);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_ERROR, "GPU Fence Failed: %s", failure_reason);
                ImGui::Unindent();
            } else {
                ImGui::Indent();
                ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
                ImGui::TextUnformatted(ICON_FK_OK);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "GPU Fence Active");
                ImGui::Unindent();
            }
        }

        ImGui::Spacing();

        DrawFrameTimeGraph();

        std::ostringstream oss;

        // Present Duration Display
        oss.str("");
        oss.clear();
        oss << "Present Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_present_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Frame Duration Display
        oss.str("");
        oss.clear();
        oss << "Frame Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_frame_time_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // GPU Duration Display (only show if measurement is enabled and has data)
        if (settings::g_mainTabSettings.gpu_measurement_enabled.GetValue() != 0 && ::g_gpu_duration_ns.load() > 0) {
            oss.str("");
            oss.clear();
            oss << "GPU Duration: " << std::fixed << std::setprecision(3)
                << (1.0 * ::g_gpu_duration_ns.load() / utils::NS_TO_MS) << " ms";
            ImGui::TextUnformatted(oss.str().c_str());
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Time from Present call to GPU completion (D3D11 only, requires Windows 10+)");
            }

            // Sim-to-Display Latency (only show if we have valid measurement)
            if (::g_sim_to_display_latency_ns.load() > 0) {
                oss.str("");
                oss.clear();
                oss << "Sim-to-Display Latency: " << std::fixed << std::setprecision(3)
                    << (1.0 * ::g_sim_to_display_latency_ns.load() / utils::NS_TO_MS) << " ms";
                ImGui::TextUnformatted(oss.str().c_str());
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Time from simulation start to frame displayed (includes GPU work and present)");
                }

                // GPU Late Time (how much later GPU finishes compared to Present)
                oss.str("");
                oss.clear();
                oss << "GPU Late Time: " << std::fixed << std::setprecision(3)
                    << (1.0 * ::g_gpu_late_time_ns.load() / utils::NS_TO_MS) << " ms";
                ImGui::TextUnformatted(oss.str().c_str());
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How much later GPU completion finishes compared to Present\n0 ms = GPU finished before Present\n>0 ms = GPU finished after Present (GPU is late)");
                }
            }
        }

        oss.str("");
        oss.clear();
        oss << "Simulation Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_simulation_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Reshade Overhead Display
        oss.str("");
        oss.clear();
        oss << "Render Submit Duration: " << std::fixed << std::setprecision(3)
            << (1.0 * ::g_render_submit_duration_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Reshade Overhead Display
        oss.str("");
        oss.clear();
        oss << "Reshade Overhead Duration: " << std::fixed << std::setprecision(3)
            << ((1.0 * ::g_reshade_overhead_duration_ns.load() - ::fps_sleep_before_on_present_ns.load()
                - ::fps_sleep_after_on_present_ns.load())
                / utils::NS_TO_MS)
            << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        oss.str("");
        oss.clear();
        oss << "FPS Limiter Sleep Duration (before onPresent): " << std::fixed << std::setprecision(3)
            << (1.0 * ::fps_sleep_before_on_present_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // FPS Limiter Start Duration Display
        oss.str("");
        oss.clear();
        oss << "FPS Limiter Sleep Duration (after onPresent): " << std::fixed << std::setprecision(3)
            << (1.0 * ::fps_sleep_after_on_present_ns.load() / utils::NS_TO_MS) << " ms";
        ImGui::TextUnformatted(oss.str().c_str());
        ImGui::SameLine();
        ImGui::TextColored(ui::colors::TEXT_VALUE, "(smoothed)");

        // Simulation Start to Present Latency Display
        oss.str("");
        oss.clear();
        // Calculate latency: frame_time - sleep duration after onPresent
        float current_fps = 0.0f;
        const uint32_t head = ::g_perf_ring_head.load(std::memory_order_acquire);
        if (head > 0) {
            const uint32_t last_idx = (head - 1) & (::kPerfRingCapacity - 1);
            const ::PerfSample& last_sample = ::g_perf_ring[last_idx];
            current_fps = 1.0f / last_sample.dt;
        }

        if (current_fps > 0.0f) {
            float frame_time_ms = 1000.0f / current_fps;
            float sleep_duration_ms = static_cast<float>(::fps_sleep_after_on_present_ns.load()) / utils::NS_TO_MS;
            float latency_ms = frame_time_ms - sleep_duration_ms;

            static double sim_start_to_present_latency_ms = 0.0;
            sim_start_to_present_latency_ms = (sim_start_to_present_latency_ms * 0.99 + latency_ms * 0.01);
            oss << "Sim Start to Present Latency: " << std::fixed << std::setprecision(3) << sim_start_to_present_latency_ms
                << " ms";
            ImGui::TextUnformatted(oss.str().c_str());
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "(frame_time - sleep_duration)");
        }

        // Flip State Display (renamed from DXGI Composition)
        const char* flip_state_str = "Unknown";
        int current_api = g_last_reshade_device_api.load();
        DxgiBypassMode flip_state = GetFlipStateForAPI(current_api);

        switch (flip_state) {
            case DxgiBypassMode::kUnset:                    flip_state_str = "Unset"; break;
            case DxgiBypassMode::kComposed:                 flip_state_str = "Composed Flip"; break;
            case DxgiBypassMode::kOverlay:                  flip_state_str = "MPO Independent Flip"; break;
            case DxgiBypassMode::kIndependentFlip:          flip_state_str = "Legacy Independent Flip"; break;
            case DxgiBypassMode::kQueryFailedSwapchainNull: flip_state_str = "Query Failed: Swapchain Null"; break;
            case DxgiBypassMode::kQueryFailedNoMedia:       flip_state_str = "Query Failed: No Media Interface"; break;
            case DxgiBypassMode::kQueryFailedNoSwapchain1:  flip_state_str = "Query Failed: No Swapchain1"; break;
            case DxgiBypassMode::kQueryFailedNoStats:       flip_state_str = "Query Failed: No Statistics"; break;
            case DxgiBypassMode::kUnknown:
            default:                                        flip_state_str = "Unknown"; break;
        }

        oss.str("");
        oss.clear();
        oss << "Status: " << flip_state_str;

        // Color code based on flip state
        if (flip_state == DxgiBypassMode::kComposed) {
            // Composed Flip - Red
            ImGui::TextColored(ui::colors::FLIP_COMPOSED, "%s", oss.str().c_str());
        } else if (flip_state == DxgiBypassMode::kOverlay || flip_state == DxgiBypassMode::kIndependentFlip) {
            // Independent Flip modes - Green
            ImGui::TextColored(ui::colors::FLIP_INDEPENDENT, "%s", oss.str().c_str());
        } else if (flip_state == DxgiBypassMode::kQueryFailedSwapchainNull ||
                   flip_state == DxgiBypassMode::kQueryFailedNoSwapchain1 ||
                   flip_state == DxgiBypassMode::kQueryFailedNoMedia ||
                   flip_state == DxgiBypassMode::kQueryFailedNoStats) {
            // Query Failed - Red
            ImGui::TextColored(ui::colors::TEXT_ERROR, "%s", oss.str().c_str());
        } else {
            // Unknown/Unset - Yellow
            ImGui::TextColored(ui::colors::FLIP_UNKNOWN, "%s", oss.str().c_str());
        }
    }

    ImGui::Spacing();

    // Refresh Rate Monitor Section
    if (ImGui::CollapsingHeader("Refresh Rate Monitor", ImGuiTreeNodeFlags_None)) {
        // Start/Stop monitoring controls
        bool is_monitoring = dxgi::fps_limiter::IsRefreshRateMonitoringActive();

        if (ImGui::Button(is_monitoring ? ICON_FK_CANCEL " Stop Monitoring" : ICON_FK_PLUS " Start Monitoring")) {
            if (is_monitoring) {
                dxgi::fps_limiter::StopRefreshRateMonitoring();
            } else {
                dxgi::fps_limiter::StartRefreshRateMonitoring();
            }
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Measures actual display refresh rate by calling WaitForVBlank in a loop.\n"
                "This shows the real refresh rate which may differ from the configured rate\n"
                "due to VRR, power management, or other factors.");
        }

        ImGui::SameLine();

        // Status display
        std::string status = dxgi::fps_limiter::GetRefreshRateStatusString();
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "Status: %s", status.c_str());

        // Get refresh rate statistics
        auto stats = dxgi::fps_limiter::GetRefreshRateStats();

        if (stats.is_valid && stats.sample_count > 0) {
            ImGui::Spacing();

            // Current refresh rate (large, prominent display)
            ImGui::Text("Measured Refresh Rate:");
            ImGui::SameLine();
            ImGui::TextColored(ui::colors::TEXT_HIGHLIGHT, "%.1f Hz", stats.smoothed_rate);

            // Detailed statistics
            ImGui::Indent();
            ImGui::Text("Current: %.1f Hz", stats.current_rate);
            ImGui::Text("Min: %.1f Hz", stats.min_rate);
            ImGui::Text("Max: %.1f Hz", stats.max_rate);
            ImGui::Text("Samples: %u", stats.sample_count);
            ImGui::Unindent();

            // VRR detection hint
            if (stats.max_rate > stats.min_rate + 1.0) {
                ImGui::Spacing();
                ui::colors::PushIconColor(ui::colors::ICON_SUCCESS);
                ImGui::TextUnformatted(ICON_FK_OK);
                ui::colors::PopIconColor();
                ImGui::SameLine();
                ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Variable Refresh Rate (VRR) detected");
            }
        } else if (is_monitoring) {
            ImGui::Spacing();
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Collecting data...");
        }
        ImGui::Unindent();  // Unindent content
    }
    ui::colors::PopNestedHeaderColors();  // Restore default header colors
    ImGui::Unindent();  // Unindent nested header section
}


void DrawAdhdMultiMonitorControls(bool hasBlackCurtainSetting) {
    // Check if multiple monitors are available
    bool hasMultipleMonitors = adhd_multi_monitor::api::HasMultipleMonitors();

    if (!hasMultipleMonitors) {
        return;
    }
    if (hasBlackCurtainSetting) ImGui::SameLine();

    // Main ADHD mode checkbox
    bool adhdEnabled = settings::g_mainTabSettings.adhd_multi_monitor_enabled.GetValue();
    if (ImGui::Checkbox("ADHD Multi-Monitor Mode", &adhdEnabled)) {
        settings::g_mainTabSettings.adhd_multi_monitor_enabled.SetValue(adhdEnabled);
        //adhd_multi_monitor::api::SetEnabled(adhdEnabled);
        LogInfo("ADHD Multi-Monitor Mode %s", adhdEnabled ? "enabled" : "disabled");
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Covers secondary monitors with a black window to reduce distractions while playing this game.");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Similar to Special-K's ADHD Multi-Monitor Mode.\nThe black background window will automatically position "
            "itself to cover all monitors except the one where your game is running.");
    }
}

}  // namespace ui::new_ui
