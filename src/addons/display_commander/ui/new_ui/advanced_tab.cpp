#include "advanced_tab.hpp"
#include "../../display/dpi_management.hpp"
#include "../../globals.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../latency/latency_manager.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../presentmon/presentmon_manager.hpp"
#include "../../res/forkawesome.h"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../utils/detour_call_tracker.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/mpo_registry.hpp"
#include "../../utils/process_window_enumerator.hpp"
#include "../../utils/timing.hpp"
#include "settings_wrapper.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <set>
#include <sstream>
#include <string>

#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace ui::new_ui {

using namespace display_commander::ui;

void DrawFeaturesEnabledByDefault(display_commander::ui::IImGuiWrapper& imgui);
void DrawAdvancedTabSettingsSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawContinuousMonitoringSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawHdrDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);
void DrawMpoSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawNvapiSettings(display_commander::ui::IImGuiWrapper& imgui);
void DrawNewExperimentalFeatures(display_commander::ui::IImGuiWrapper& imgui);

void InitAdvancedTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        // Settings already loaded at startup
        settings_loaded = true;

        // Start PresentMon worker if the setting is already enabled
        // This ensures PresentMon starts on game restart if the setting was previously enabled
        if (settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()) {
            LogInfo("InitAdvancedTab() - PresentMon tracing setting is enabled, starting worker");
            presentmon::g_presentMonManager.StartWorker();
        }
    }
}

void DrawAdvancedTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    if (imgui.CollapsingHeader("Features Enabled By Default", wrapper_flags::TreeNodeFlags_None)) {
        DrawFeaturesEnabledByDefault(imgui);
    }
    imgui.Spacing();

    // Advanced Settings Section
    if (imgui.CollapsingHeader("Advanced Settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawAdvancedTabSettingsSection(imgui);
    }

    imgui.Spacing();

    // Continuous monitoring Section
    if (imgui.CollapsingHeader("Triggers Settings (for debugging purposes)", wrapper_flags::TreeNodeFlags_None)) {
        DrawContinuousMonitoringSection(imgui);
    }

    imgui.Spacing();

    // HDR and Display Settings Section
    if (imgui.CollapsingHeader("HDR and Display Settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawHdrDisplaySettings(api, imgui);
    }

    imgui.Spacing();

    if (enabled_experimental_features) {
        // Disable MPO (fix black screen on multimonitor) Section
        if (imgui.CollapsingHeader("Disable MPO (fix black screen issues on multimonitor setup)",
                                   wrapper_flags::TreeNodeFlags_None)) {
            DrawMpoSection(imgui);
        }

        imgui.Spacing();
    }

    // NVAPI Settings Section - only show if game is in NVAPI game list
    DrawNvapiSettings(imgui);

    imgui.Spacing();

    // New Experimental Features Section
    if (imgui.CollapsingHeader("New Experimental Features", wrapper_flags::TreeNodeFlags_None)) {
        DrawNewExperimentalFeatures(imgui);
    }

    imgui.Spacing();

    // Debug Tools Section
    if (imgui.CollapsingHeader("Debug Tools", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();

        if (imgui.Button(ICON_FK_FILE " Log All Processes & Windows")) {
            LogInfo("Button clicked: Starting process and window enumeration...");
            display_commander::utils::LogAllProcessesAndWindows();
            LogInfo("Button handler: Process and window enumeration function returned");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Enumerates all running processes and their windows, logging detailed information to the log file.\n"
                "Useful for debugging overlay detection and window management issues.");
        }

        imgui.Spacing();

        if (imgui.Button(ICON_FK_SEARCH " Print Detour Call Tracker Info")) {
            const uint64_t now_ns = static_cast<uint64_t>(utils::get_now_ns());
            LogInfo("=== Detour Call Tracker (manual trigger) ===");

            std::string all_latest = detour_call_tracker::FormatAllLatestCalls(now_ns);
            if (!all_latest.empty()) {
                std::istringstream iss(all_latest);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        LogInfo("%s", line.c_str());
                    }
                }
            }

            std::string recent = detour_call_tracker::FormatRecentDetourCalls(now_ns, 64);
            if (!recent.empty()) {
                std::istringstream iss(recent);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        LogInfo("%s", line.c_str());
                    }
                }
            }

            std::string undestroyed = detour_call_tracker::FormatUndestroyedGuards(now_ns);
            if (!undestroyed.empty()) {
                std::istringstream iss(undestroyed);
                std::string line;
                while (std::getline(iss, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        LogInfo("%s", line.c_str());
                    }
                }
            }

            LogInfo("=== End Detour Call Tracker ===");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Log detour call tracker info to ReShade log: all call sites (by last call), recent calls, and "
                "undestroyed guards.\nUseful for debugging stuck threads or crashes without proper cleanup.");
        }

        imgui.Unindent();
    }
}

void DrawFeaturesEnabledByDefault(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Prevent Fullscreen
    CheckboxSetting(settings::g_advancedTabSettings.prevent_fullscreen, "Prevent Fullscreen", &imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Prevent exclusive fullscreen; keep borderless/windowed for stability and HDR.");
    }

    CheckboxSetting(settings::g_advancedTabSettings.prevent_always_on_top, "Prevent Always On Top", &imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Prevents windows from becoming always on top, even if they are moved or resized.");
    }

    CheckboxSetting(settings::g_advancedTabSettings.prevent_minimize, "Prevent Minimize", &imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("Prevents the game window from being minimized (e.g. via taskbar or system menu).");
    }

    imgui.Unindent();
}

void DrawAdvancedTabSettingsSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Safemode setting
    if (CheckboxSetting(settings::g_advancedTabSettings.safemode, "Safemode (requires restart)", &imgui)) {
        LogInfo("Safemode setting changed to: %s",
                settings::g_advancedTabSettings.safemode.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Safemode disables all auto-apply settings and sets FPS limiter to disabled.\n"
            "When enabled, it will automatically set itself to 0 and disable:\n"
            "- Auto-apply resolution changes\n"
            "- Auto-apply refresh rate changes\n"
            "- Apply display settings at start\n"
            "- FPS limiter mode (set to disabled)\n\n"
            "This setting requires a game restart to take effect.");
    }

    // DLLs to load before Display Commander
    std::string dlls_to_load = settings::g_advancedTabSettings.dlls_to_load_before.GetValue();
    char dlls_buffer[512] = {0};
    strncpy_s(dlls_buffer, sizeof(dlls_buffer), dlls_to_load.c_str(), _TRUNCATE);
    if (imgui.InputText("DLLs to Load Before Display Commander", dlls_buffer, sizeof(dlls_buffer))) {
        settings::g_advancedTabSettings.dlls_to_load_before.SetValue(std::string(dlls_buffer));
        LogInfo("DLLs to load before set to: %s", dlls_buffer);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Comma or semicolon-separated list of DLL names to wait for before Display Commander continues "
            "initialization.\n"
            "Example: dll1.dll, dll2.dll, dll3.dll or dll1.dll; dll2.dll; dll3.dll\n"
            "Display Commander will wait for each DLL to be loaded (up to 30 seconds per DLL) before proceeding.\n"
            "This happens before the DLL loading delay.\n\n"
            "This setting requires a game restart to take effect.");
    }

    // DLL loading delay setting
    int delay_ms = settings::g_advancedTabSettings.dll_loading_delay_ms.GetValue();
    if (imgui.SliderInt("DLL Loading Delay (ms)", &delay_ms, 0, 10000, delay_ms == 0 ? "No delay" : "%d ms")) {
        settings::g_advancedTabSettings.dll_loading_delay_ms.SetValue(delay_ms);
        LogInfo("DLL loading delay set to %d ms", delay_ms);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Delay before installing LoadLibrary hooks (in milliseconds).\n"
            "This can help with compatibility issues by allowing other DLLs to load first.\n"
            "Set to 0 to disable delay.\n\n"
            "This setting requires a game restart to take effect.");
    }

    // Suppress MinHook setting
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_minhook, "Suppress MinHook Initialization", &imgui)) {
        LogInfo("Suppress MinHook setting changed to: %s",
                settings::g_advancedTabSettings.suppress_minhook.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Suppress all MinHook initialization calls (MH_Initialize).\n"
            "When enabled, all hook functions will skip MinHook initialization.\n"
            "This can help with compatibility issues or debugging.\n"
            "This setting is automatically enabled when safemode is active.\n\n"
            "This setting requires a game restart to take effect.");
    }

    imgui.Spacing();

    // Suppress Windows.Gaming.Input (force XInput for continue rendering with gamepad)
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_windows_gaming_input,
                        "Suppress Windows.Gaming.Input (use XInput)", &imgui)) {
        LogInfo("Suppress Windows.Gaming.Input setting changed to: %s",
                settings::g_advancedTabSettings.suppress_windows_gaming_input.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Suppress Windows.Gaming.Input.dll so the game uses XInput instead.\n"
            "When enabled, continue rendering in background works with gamepad (WGI loses input when the window is "
            "inactive).\n"
            "Default: on.");
    }

    imgui.Spacing();

    // Auto-hide Discord Overlay setting
    if (CheckboxSetting(settings::g_advancedTabSettings.auto_hide_discord_overlay, "Auto-hide Discord Overlay",
                        &imgui)) {
        LogInfo("Auto-hide Discord Overlay setting changed to: %s",
                settings::g_advancedTabSettings.auto_hide_discord_overlay.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Automatically hide Discord Overlay window when it overlaps with the game window.\n"
            "This prevents the overlay from interfering with MPO iFlip and can improve performance.\n"
            "Similar to Special-K's behavior when AllowWindowedMode=false.\n\n"
            "The check runs every second in the continuous monitoring thread.");
    }

    imgui.Spacing();

    // Suppress Window Changes setting
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_window_changes, "Suppress Window Changes", &imgui)) {
        LogInfo("Suppress Window Changes setting changed to: %s",
                settings::g_advancedTabSettings.suppress_window_changes.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Suppresses automatic window position, size, and style changes from continuous monitoring.\n"
            "When enabled, ApplyWindowChange will not be called automatically.\n"
            "This is a compatibility feature for cases where automatic window management causes issues.\n\n"
            "Default: disabled (window changes are applied automatically).");
    }

    imgui.Spacing();

    // Win+Up grace period (global setting, stored in Display Commander folder)
    {
        int grace = settings::g_advancedTabSettings.win_up_grace_seconds.GetValue();
        const char* format = (grace >= 61) ? "Forever" : "%d s";
        if (imgui.SliderInt("Win+Up grace period (after leaving foreground)", &grace, 0, 61, format)) {
            if (grace < 0) {
                grace = 0;
            } else if (grace > 61) {
                grace = 61;
            }
            settings::g_advancedTabSettings.win_up_grace_seconds.SetValue(grace);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "For borderless windows: how long after the game loses focus Win+Up (restore) still works.\n"
                "0 = only when game is in foreground; 1-60 = seconds; 61 = Forever (Win+Up always works).\n"
                "Stored in Display Commander config (global). Default: 1 s.");
        }
    }

    imgui.Spacing();

    // PresentMon ETW Tracing setting
    if (CheckboxSetting(settings::g_advancedTabSettings.enable_presentmon_tracing, "Enable PresentMon ETW Tracing",
                        &imgui)) {
        LogInfo("PresentMon ETW tracing setting changed to: %s",
                settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue() ? "enabled" : "disabled");

        // Start or stop PresentMon based on setting
        if (settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()) {
            presentmon::g_presentMonManager.StartWorker();
        } else {
            presentmon::g_presentMonManager.StopWorker();
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Enable PresentMon ETW (Event Tracing for Windows) tracing for presentation tracking.\n"
            "Similar to Special-K's PresentMon integration.\n\n"
            "FEATURES:\n"
            "- Tracks presentation timing and frame pacing\n"
            "- Provides latency and flip information\n"
            "- Useful for VRR indicator on D3D12 games\n"
            "- Required for accurate presentation stats on non-NVIDIA hardware\n\n"
            "STATUS:\n"
            "- ETW session is started in a background thread\n"
            "- Flip mode is best-effort (depends on ETW provider fields)\n"
            "- Default: enabled\n\n"
            "Note: Requires appropriate Windows permissions for ETW tracing.");
    }

    // Show PresentMon status
    if (presentmon::g_presentMonManager.IsRunning()) {
        imgui.SameLine();
        imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " ACTIVE");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("PresentMon worker thread is currently running.");
        }

        // Show detailed debug info when active in advanced tab
        imgui.Indent();
        presentmon::PresentMonFlipState pm_flip_state;
        presentmon::PresentMonDebugInfo pm_debug_info;
        bool has_pm_flip_state = presentmon::g_presentMonManager.GetFlipState(pm_flip_state);
        presentmon::g_presentMonManager.GetDebugInfo(pm_debug_info);

        imgui.TextColored(wrapper_colors::TEXT_LABEL, "ETW Status:");
        imgui.SameLine();
        if (!pm_debug_info.etw_session_name.empty()) {
            imgui.Text("%s [%s]", pm_debug_info.etw_session_status.c_str(), pm_debug_info.etw_session_name.c_str());
        } else {
            imgui.Text("%s", pm_debug_info.etw_session_status.c_str());
        }

        // Display list of DC_ ETW sessions
        if (!pm_debug_info.dc_etw_sessions.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL,
                              "DC_ ETW Sessions (%zu):", pm_debug_info.dc_etw_sessions.size());
            imgui.Indent();
            for (const auto& session_name : pm_debug_info.dc_etw_sessions) {
                imgui.PushID(session_name.c_str());

                // Check if this is the current session
                bool is_current_session = (session_name == pm_debug_info.etw_session_name);

                // Display session name
                imgui.Text("  • %s", session_name.c_str());
                imgui.SameLine();

                // Add close button (X) - disable for current session
                if (is_current_session) {
                    imgui.BeginDisabled();
                }

                // Small button with X icon
                imgui.PushStyleColor(4, ImGuiWrapperColor{0.7f, 0.2f, 0.2f, 0.6f});
                imgui.PushStyleColor(5, ImGuiWrapperColor{0.9f, 0.3f, 0.3f, 0.8f});
                imgui.PushStyleColor(6, wrapper_colors::TEXT_ERROR);

                if (imgui.SmallButton(ICON_FK_CANCEL)) {
                    // Convert narrow string to wide string for StopEtwSessionByName
                    int wide_len = MultiByteToWideChar(CP_UTF8, 0, session_name.c_str(), -1, nullptr, 0);
                    if (wide_len > 0) {
                        std::vector<wchar_t> wide_name(wide_len);
                        MultiByteToWideChar(CP_UTF8, 0, session_name.c_str(), -1, wide_name.data(), wide_len);
                        presentmon::PresentMonManager::StopEtwSessionByName(wide_name.data());
                        LogInfo("Stopped ETW session: %s", session_name.c_str());
                    }
                }

                imgui.PopStyleColor(3);

                if (is_current_session) {
                    imgui.EndDisabled();
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip("Cannot stop current session");
                    }
                } else {
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltip("Stop ETW session: %s", session_name.c_str());
                    }
                }

                imgui.PopID();
            }
            imgui.Unindent();
        }

        if (!pm_debug_info.last_error.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_ERROR, "Last Error: %s", pm_debug_info.last_error.c_str());
        }

        imgui.TextColored(wrapper_colors::TEXT_LABEL, "Events:");
        imgui.SameLine();
        imgui.Text("%llu (pid=%llu)", static_cast<unsigned long long>(pm_debug_info.events_processed),
                   static_cast<unsigned long long>(pm_debug_info.events_processed_for_current_pid));

        imgui.TextColored(wrapper_colors::TEXT_LABEL, "Last Event PID:");
        imgui.SameLine();
        imgui.Text("%u", static_cast<unsigned int>(pm_debug_info.last_event_pid));

        imgui.TextColored(wrapper_colors::TEXT_LABEL, "Providers:");
        imgui.SameLine();
        imgui.Text("DxgKrnl=%llu, DXGI=%llu, DWM=%llu", static_cast<unsigned long long>(pm_debug_info.events_dxgkrnl),
                   static_cast<unsigned long long>(pm_debug_info.events_dxgi),
                   static_cast<unsigned long long>(pm_debug_info.events_dwm));

        if (!pm_debug_info.last_graphics_provider.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Last Graphics Event:");
            imgui.SameLine();
            imgui.Text("%s | id=%u | pid=%u", pm_debug_info.last_graphics_provider.c_str(),
                       static_cast<unsigned int>(pm_debug_info.last_graphics_event_id),
                       static_cast<unsigned int>(pm_debug_info.last_graphics_event_pid));
        }
        if (!pm_debug_info.last_graphics_provider_name.empty() || !pm_debug_info.last_graphics_event_name.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Graphics Schema:");
            imgui.SameLine();
            imgui.Text("%s :: %s",
                       pm_debug_info.last_graphics_provider_name.empty()
                           ? "(unknown provider)"
                           : pm_debug_info.last_graphics_provider_name.c_str(),
                       pm_debug_info.last_graphics_event_name.empty() ? "(unknown event)"
                                                                      : pm_debug_info.last_graphics_event_name.c_str());
        }
        imgui.TextColored(wrapper_colors::TEXT_LABEL, "Graphics Props:");
        imgui.SameLine();
        if (!pm_debug_info.last_graphics_props.empty()) {
            imgui.TextWrapped("%s", pm_debug_info.last_graphics_props.c_str());
        } else {
            imgui.TextColored(wrapper_colors::TEXT_DIMMED, "(none)");
        }

        if (!pm_debug_info.last_provider.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Last Event:");
            imgui.SameLine();
            imgui.Text("%s | id=%u", pm_debug_info.last_provider.c_str(),
                       static_cast<unsigned int>(pm_debug_info.last_event_id));
        }
        if (!pm_debug_info.last_provider_name.empty() || !pm_debug_info.last_event_name.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Schema:");
            imgui.SameLine();
            imgui.Text(
                "%s :: %s",
                pm_debug_info.last_provider_name.empty() ? "(unknown provider)"
                                                         : pm_debug_info.last_provider_name.c_str(),
                pm_debug_info.last_event_name.empty() ? "(unknown event)" : pm_debug_info.last_event_name.c_str());
        }
        if (!pm_debug_info.last_interesting_props.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Props:");
            imgui.SameLine();
            imgui.TextWrapped("%s", pm_debug_info.last_interesting_props.c_str());
        }
        if (!pm_debug_info.last_present_mode_value.empty()) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Last PresentMode:");
            imgui.SameLine();
            imgui.Text("%s", pm_debug_info.last_present_mode_value.c_str());
        }

        if (has_pm_flip_state) {
            imgui.TextColored(wrapper_colors::TEXT_LABEL, "Flip Mode:");
            imgui.SameLine();
            imgui.Text("%s", pm_flip_state.present_mode_str.c_str());
        } else {
            imgui.TextColored(wrapper_colors::TEXT_DIMMED, "Flip Mode: (No data yet)");
        }

        // DWM Flip Compatibility (separate from flip-state)
        presentmon::PresentMonFlipCompatibility pm_flip_compat;
        if (presentmon::g_presentMonManager.GetFlipCompatibility(pm_flip_compat)) {
            imgui.Spacing();
            if (imgui.CollapsingHeader("Flip Compatibility (DWM)", wrapper_flags::TreeNodeFlags_DefaultOpen)) {
                imgui.Indent();

                // Age
                LONGLONG now_ns = utils::get_now_ns();
                double age_ms =
                    static_cast<double>(now_ns - static_cast<LONGLONG>(pm_flip_compat.last_update_time_ns)) / 1000000.0;
                imgui.TextColored(wrapper_colors::TEXT_DIMMED, "Last update: %.1f ms ago", age_ms);

                imgui.Text("surfaceLuid: 0x%llx", static_cast<unsigned long long>(pm_flip_compat.surface_luid));
                imgui.Text("Surface: %ux%u  PixelFormat=%u  ColorSpace=%u  Flags=0x%x", pm_flip_compat.surface_width,
                           pm_flip_compat.surface_height, pm_flip_compat.pixel_format, pm_flip_compat.color_space,
                           pm_flip_compat.flags);

                imgui.Text("IsDirectFlipCompatible: %s", pm_flip_compat.is_direct_flip_compatible ? "Yes" : "No");
                imgui.Text("IsAdvancedDirectFlipCompatible: %s",
                           pm_flip_compat.is_advanced_direct_flip_compatible ? "Yes" : "No");
                imgui.Text("IsOverlayCompatible: %s", pm_flip_compat.is_overlay_compatible ? "Yes" : "No");
                imgui.Text("IsOverlayRequired: %s", pm_flip_compat.is_overlay_required ? "Yes" : "No");
                imgui.Text("fNoOverlappingContent: %s", pm_flip_compat.no_overlapping_content ? "Yes" : "No");

                imgui.Spacing();
                if (imgui.CollapsingHeader("Recent surfaces (last 1h)", wrapper_flags::TreeNodeFlags_DefaultOpen)) {
                    std::vector<presentmon::PresentMonSurfaceCompatibilitySummary> surfaces;
                    presentmon::g_presentMonManager.GetRecentFlipCompatibilitySurfaces(surfaces, 3600000);

                    imgui.TextColored(wrapper_colors::TEXT_DIMMED, "Surfaces: %d", static_cast<int>(surfaces.size()));

                    if (imgui.BeginTable("##pm_surfaces", 10,
                                         wrapper_flags::TableFlags_RowBg | wrapper_flags::TableFlags_Borders
                                             | wrapper_flags::TableFlags_SizingFixedFit
                                             | wrapper_flags::TableFlags_ScrollY,
                                         ImGuiWrapperVec2{0.f, 260.f})) {
                        imgui.TableSetupColumn("Age(ms)");
                        imgui.TableSetupColumn("surfaceLuid");
                        imgui.TableSetupColumn("hwnd");
                        imgui.TableSetupColumn("WxH");
                        imgui.TableSetupColumn("PF");
                        imgui.TableSetupColumn("CS");
                        imgui.TableSetupColumn("Flags");
                        imgui.TableSetupColumn("Direct");
                        imgui.TableSetupColumn("Overlay");
                        imgui.TableSetupColumn("Count");
                        imgui.TableHeadersRow();

                        for (const auto& s : surfaces) {
                            const double age_ms =
                                static_cast<double>(now_ns - static_cast<LONGLONG>(s.last_update_time_ns)) / 1000000.0;

                            imgui.TableNextRow();

                            imgui.TableSetColumnIndex(0);
                            imgui.Text("%.0f", age_ms);

                            imgui.TableSetColumnIndex(1);
                            imgui.Text("0x%llx", static_cast<unsigned long long>(s.surface_luid));

                            imgui.TableSetColumnIndex(2);
                            if (s.hwnd != 0) {
                                imgui.Text("0x%llx", static_cast<unsigned long long>(s.hwnd));
                            } else {
                                imgui.TextColored(wrapper_colors::TEXT_DIMMED, "(unknown)");
                            }

                            imgui.TableSetColumnIndex(3);
                            imgui.Text("%ux%u", s.surface_width, s.surface_height);

                            imgui.TableSetColumnIndex(4);
                            imgui.Text("%u", s.pixel_format);

                            imgui.TableSetColumnIndex(5);
                            imgui.Text("%u", s.color_space);

                            imgui.TableSetColumnIndex(6);
                            imgui.Text("0x%x", s.flags);

                            imgui.TableSetColumnIndex(7);
                            imgui.Text("%s%s", s.is_direct_flip_compatible ? "Y" : "N",
                                       s.is_advanced_direct_flip_compatible ? " (adv)" : "");

                            imgui.TableSetColumnIndex(8);
                            imgui.Text("%s%s", s.is_overlay_compatible ? "Y" : "N",
                                       s.is_overlay_required ? " (req)" : "");

                            imgui.TableSetColumnIndex(9);
                            imgui.Text("%llu", static_cast<unsigned long long>(s.count));
                        }

                        imgui.EndTable();
                    }
                }

                imgui.Unindent();
            }
        }

        imgui.Spacing();
        if (imgui.CollapsingHeader("ETW Event Type Explorer (Debug)", wrapper_flags::TreeNodeFlags_None)) {
            static bool s_graphics_only = true;
            imgui.Checkbox("Graphics-only (DxgKrnl/DXGI/DWM)", &s_graphics_only);

            std::vector<presentmon::PresentMonEventTypeSummary> types;
            presentmon::g_presentMonManager.GetEventTypeSummaries(types, s_graphics_only);

            imgui.TextColored(wrapper_colors::TEXT_DIMMED, "Cached event types: %d", static_cast<int>(types.size()));

            if (imgui.BeginTable("##pm_event_types", 7,
                                 wrapper_flags::TableFlags_RowBg | wrapper_flags::TableFlags_Borders
                                     | wrapper_flags::TableFlags_SizingFixedFit | wrapper_flags::TableFlags_ScrollY,
                                 ImGuiWrapperVec2{0.f, 2220.f})) {
                imgui.TableSetupColumn("Count");
                imgui.TableSetupColumn("Provider");
                imgui.TableSetupColumn("EventId");
                imgui.TableSetupColumn("Task");
                imgui.TableSetupColumn("Op");
                imgui.TableSetupColumn("Keyword");
                imgui.TableSetupColumn("Props", wrapper_flags::TableColumnFlags_WidthFixed, 600.0f);
                imgui.TableHeadersRow();

                const int max_rows = 200;
                int rows = 0;
                for (const auto& t : types) {
                    if (rows++ >= max_rows) break;
                    imgui.TableNextRow();

                    imgui.TableSetColumnIndex(0);
                    imgui.Text("%llu", static_cast<unsigned long long>(t.count));

                    imgui.TableSetColumnIndex(1);
                    if (!t.provider_name.empty()) {
                        imgui.Text("%s", t.provider_name.c_str());
                    } else {
                        imgui.Text("%s", t.provider_guid.c_str());
                    }
                    if (!t.event_name.empty() && imgui.IsItemHovered()) {
                        imgui.SetTooltip("%s", t.event_name.c_str());
                    }

                    imgui.TableSetColumnIndex(2);
                    imgui.Text("%u", static_cast<unsigned int>(t.event_id));

                    imgui.TableSetColumnIndex(3);
                    imgui.Text("%u", static_cast<unsigned int>(t.task));

                    imgui.TableSetColumnIndex(4);
                    imgui.Text("%u", static_cast<unsigned int>(t.opcode));

                    imgui.TableSetColumnIndex(5);
                    imgui.Text("0x%llx", static_cast<unsigned long long>(t.keyword));

                    imgui.TableSetColumnIndex(6);
                    imgui.TextWrapped("%s", t.props.empty() ? "(no schema/props)" : t.props.c_str());
                }

                imgui.EndTable();
            }
        }

        imgui.Unindent();
    }

    imgui.Spacing();

    // Debug Layer checkbox with warning
    imgui.TextColored(wrapper_colors::ICON_WARNING, ICON_FK_WARNING);
    imgui.SameLine();
    imgui.TextColored(wrapper_colors::ICON_WARNING, "REQUIRES SETUP:");
    imgui.SameLine();
    if (CheckboxSetting(settings::g_advancedTabSettings.debug_layer_enabled, "Enable DX11/DX12 Debug Layer", &imgui)) {
        LogInfo("Debug layer setting changed to: %s",
                settings::g_advancedTabSettings.debug_layer_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(ICON_FK_WARNING
                         " WARNING: Debug Layer Setup Required " ICON_FK_WARNING
                         "\n\n"
                         "REQUIREMENTS:\n"
                         "- Windows 11 SDK must be installed\n"
                         "- Download: https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/\n"
                         "- Install 'Graphics Tools' and 'Debugging Tools for Windows'\n\n"
                         "SETUP STEPS:\n"
                         "1. Install Windows 11 SDK with Graphics Tools\n"
                         "2. Run DbgView.exe as Administrator\n"
                         "3. Enable this setting\n"
                         "4. RESTART THE GAME for changes to take effect\n\n"
                         "FEATURES:\n"
                         "- D3D11: Adds D3D11_CREATE_DEVICE_DEBUG flag\n"
                         "- D3D12: Enables debug layer via D3D12GetDebugInterface\n"
                         "- Breaks on all severity levels (ERROR, WARNING, INFO)\n"
                         "- Debug output appears in DbgView\n\n" ICON_FK_WARNING
                         " May significantly impact performance when enabled!");
    }

    // Show status when debug layer is enabled
    if (settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
        imgui.SameLine();
        imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " ACTIVE");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Debug layer is currently ENABLED.\n"
                "- Debug output should appear in DbgView\n"
                "- Performance may be significantly reduced\n"
                "- Restart game if you just enabled this setting\n"
                "- Disable when not debugging to restore performance");
        }
    }

    // SetBreakOnSeverity checkbox (only shown when debug layer is enabled)
    if (settings::g_advancedTabSettings.debug_layer_enabled.GetValue()) {
        imgui.Indent();
        if (CheckboxSetting(settings::g_advancedTabSettings.debug_break_on_severity, "SetBreakOnSeverity (All Levels)",
                            &imgui)) {
            LogInfo("Debug break on severity setting changed to: %s",
                    settings::g_advancedTabSettings.debug_break_on_severity.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Enable SetBreakOnSeverity for all debug message levels.\n"
                "When enabled, the debugger will break on:\n"
                "- ERROR messages\n"
                "- CORRUPTION messages\n"
                "- WARNING messages\n"
                "- INFO messages\n"
                "- MESSAGE messages\n\n"
                "This setting only takes effect when debug layer is enabled.\n"
                "Requires a game restart to take effect.");
        }
        imgui.Unindent();
    }

    imgui.Unindent();
}

void DrawContinuousMonitoringSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    if (imgui.TreeNodeEx("High-frequency updates (~120 Hz)", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        CheckboxSetting(settings::g_advancedTabSettings.monitor_high_freq_enabled, "Enable high-frequency updates",
                        &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Background/foreground check, ADHD multi-monitor, keyboard tracking, hotkeys.\n"
                "Disable to reduce CPU when these features are not needed.");
        }
        SliderIntSetting(settings::g_advancedTabSettings.monitor_high_freq_interval_ms, "Interval (ms)", "%d ms",
                         &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Loop interval: 8 = ~120 Hz, 16 = ~60 Hz, 33 = ~30 Hz. When disabled, loop sleeps 50 ms.");
        }
        imgui.Unindent();
        imgui.TreePop();
    }

    if (imgui.TreeNodeEx("Per-second tasks", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        CheckboxSetting(settings::g_advancedTabSettings.monitor_per_second_enabled, "Enable per-second tasks", &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Screensaver, FPS aggregate, volume, refresh rate, VRR status, and other periodic tasks.");
        }
        SliderIntSetting(settings::g_advancedTabSettings.monitor_per_second_interval_sec, "Interval (seconds)", "%d s",
                         &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("How often the per-second block runs (1–60 seconds).");
        }
        imgui.Spacing();
        imgui.TextColored(wrapper_colors::TEXT_LABEL, "Triggers:");
        CheckboxSetting(settings::g_advancedTabSettings.monitor_screensaver, "Screensaver / display required", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_fps_aggregate, "FPS aggregate (overlay stats)", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_volume, "Volume (game & system)", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_refresh_rate, "Refresh rate stats", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_vrr_status, "VRR status (NVAPI)", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_exclusive_key_groups, "Exclusive key groups cache",
                        &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_discord_overlay, "Discord overlay auto-hide", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_reflex_auto_configure, "Reflex auto-configure", &imgui);
        CheckboxSetting(settings::g_advancedTabSettings.monitor_auto_apply_trigger,
                        "Auto-apply (HDR/resolution) trigger", &imgui);
        imgui.Unindent();
        imgui.TreePop();
    }

    if (imgui.TreeNodeEx("Display cache refresh", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        CheckboxSetting(settings::g_advancedTabSettings.monitor_display_cache, "Enable display cache refresh", &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Refreshes display list off the UI thread. Disable to reduce overhead.");
        }
        SliderIntSetting(settings::g_advancedTabSettings.monitor_display_cache_interval_sec, "Interval (seconds)",
                         "%d s", &imgui);
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("How often to refresh the display cache (1–60 seconds).");
        }
        imgui.Unindent();
        imgui.TreePop();
    }

    imgui.Unindent();
}

void DrawHdrDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    const bool is_dxgi =
        (api == display_commander::ui::GraphicsApi::D3D11 || api == display_commander::ui::GraphicsApi::D3D12
         || api == display_commander::ui::GraphicsApi::D3D10);

    // Hide HDR Capabilities
    if (CheckboxSetting(settings::g_advancedTabSettings.hide_hdr_capabilities,
                        "Hide display's HDR capabilities from game", &imgui)) {
        LogInfo("HDR hiding setting changed to: %s",
                settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue() ? "true" : "false");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Tries to prevent the game from turning on its HDR.\n"
            "Hides HDR capabilities from the game by intercepting CheckColorSpaceSupport and GetDesc calls,\n"
            "so the game may use SDR mode instead.");
    }

    // Enable Flip Chain
    if (CheckboxSetting(settings::g_advancedTabSettings.enable_flip_chain, "Enable flip chain", &imgui)) {
        LogInfo("Enable flip chain setting changed to: %s",
                settings::g_advancedTabSettings.enable_flip_chain.GetValue() ? "true" : "false");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
            "This setting requires a game restart to take effect.\n"
            "Only works with DirectX 10/11/12 (DXGI) games.");
    }

    // Disable DPI Scaling checkbox
    if (CheckboxSetting(settings::g_advancedTabSettings.disable_dpi_scaling, "Disable DPI scaling", &imgui)) {
        bool enabled = settings::g_advancedTabSettings.disable_dpi_scaling.GetValue();
        LogInfo("Disable DPI scaling setting changed to: %s", enabled ? "true" : "false");

        if (enabled) {
            display_commander::display::dpi::DisableDPIScaling();
        } else {
            display_commander::display::dpi::EnableDPIScaling();
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "Makes the process DPI-aware to prevent Windows from bitmap-scaling the application.\n"
            "Uses AppCompat registry for persistence across restarts.\n"
            "Requires a game restart to take full effect.");
    }

    if (is_dxgi) {
        imgui.Spacing();

        // Auto Color Space checkbox
        bool auto_colorspace = settings::g_advancedTabSettings.auto_colorspace.GetValue();
        if (imgui.Checkbox("Auto color space", &auto_colorspace)) {
            settings::g_advancedTabSettings.auto_colorspace.SetValue(auto_colorspace);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Automatically sets the appropriate color space on the game's swap chain based on the current format.\n"
                "- HDR10 format (R10G10B10A2) → HDR10 color space (ST2084)\n"
                "- FP16 format (R16G16B16A16) → scRGB color space (Linear)\n"
                "- SDR format (R8G8B8A8) → sRGB color space (Non-linear)\n"
                "Only works with DirectX 11/12 games.\n"
                "Applied automatically in presentBefore.");
        }
    }

    // Show upgrade status
    if (s_d3d9e_upgrade_successful.load()) {
        imgui.Indent();
        imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " D3D9 upgraded to D3D9Ex successfully");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Direct3D 9 was successfully upgraded to Direct3D 9Ex.\n"
                "Your game is now using the enhanced D3D9Ex API.");
        }
        imgui.Unindent();
    } else if (settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
        imgui.Indent();
        imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "Waiting for D3D9 device creation...");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "The upgrade will occur when the game creates a Direct3D 9 device.\n"
                "If the game is not using D3D9, this setting has no effect.");
        }
        imgui.Unindent();
    }

    imgui.Unindent();
}

void DrawMpoSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    display_commander::utils::MpoRegistryStatus status = {};
    display_commander::utils::MpoRegistryGetStatus(&status);

    imgui.TextColored(wrapper_colors::TEXT_DIMMED,
                      "MPO registry options. Check to enable each. Restart required. Requires administrator.");
    imgui.Spacing();

    // Status of the other two (Windows options)
    imgui.TextColored(wrapper_colors::TEXT_LABEL, "Status:");
    imgui.SameLine();
    imgui.Text("OverlayTestMode %s, DisableMPO %s, DisableOverlays %s", status.overlay_test_mode_5 ? "= 5" : "not set",
               status.disable_mpo ? "= 1" : "not set", status.disable_overlays ? "= 1" : "not set");
    imgui.Spacing();

    bool overlay_test_mode = status.overlay_test_mode_5;
    if (imgui.Checkbox("OverlayTestMode = 5 (Dwm)", &overlay_test_mode)) {
        if (display_commander::utils::MpoRegistrySetOverlayTestMode(overlay_test_mode)) {
            LogInfo("MPO: OverlayTestMode set via Advanced tab.");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm -> OverlayTestMode. Classic Windows option to disable MPO.");
    }

    bool disable_mpo = status.disable_mpo;
    if (imgui.Checkbox("DisableMPO = 1 (GraphicsDrivers)", &disable_mpo)) {
        if (display_commander::utils::MpoRegistrySetDisableMPO(disable_mpo)) {
            LogInfo("MPO: DisableMPO set via Advanced tab.");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip("HKLM\\...\\GraphicsDrivers -> DisableMPO. Classic Windows option to disable MPO.");
    }

    bool disable_overlays = status.disable_overlays;
    if (imgui.Checkbox("DisableOverlays = 1 (Disable MPO Windows 11 25H2 solution)", &disable_overlays)) {
        if (display_commander::utils::MpoRegistrySetDisableOverlays(disable_overlays)) {
            LogInfo("MPO: DisableOverlays set via Advanced tab.");
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "HKLM\\...\\GraphicsDrivers -> DisableOverlays. Disables all overlays (Discord, GPU overlays); may affect "
            "VRR.");
    }

    imgui.Unindent();
}

void DrawNvapiSettings(display_commander::ui::IImGuiWrapper& imgui) {
    uint64_t now_ns = utils::get_now_ns();

    if (IsGameInNvapiAutoEnableList(GetCurrentProcessName())) {
        if (imgui.CollapsingHeader("NVAPI Settings", wrapper_flags::TreeNodeFlags_None)) {
            imgui.Indent();
            // NVAPI Auto-enable checkbox
            if (CheckboxSetting(settings::g_advancedTabSettings.nvapi_auto_enable_enabled,
                                "Enable NVAPI Auto-enable for Games", &imgui)) {
                LogInfo("NVAPI Auto-enable setting changed to: %s",
                        settings::g_advancedTabSettings.nvapi_auto_enable_enabled.GetValue() ? "true" : "false");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Automatically enable NVAPI features for supported games when they are launched.");
            }

            // Display current game status
            imgui.Spacing();
            std::string gameStatus = GetNvapiAutoEnableGameStatus();
            bool isGameSupported = IsGameInNvapiAutoEnableList(GetCurrentProcessName());

            if (isGameSupported) {
                imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " Current Game: %s", gameStatus.c_str());
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("This game is supported for NVAPI auto-enable features.");
                }
                // Warning about Alt+Enter requirement
                imgui.Spacing();
                imgui.TextColored(ImGuiWrapperColor{1.0f, 0.8f, 0.0f, 1.0f},
                                  ICON_FK_WARNING " Warning: Requires pressing Alt+Enter once");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "Press Alt-Enter to enable HDR.\n"
                        "This is required for proper HDR functionality.");
                }

            } else {
                imgui.TextColored(wrapper_colors::TEXT_DIMMED, ICON_FK_CANCEL " Current Game: %s", gameStatus.c_str());
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("This game is not in the NVAPI auto-enable supported games list.");
                }
            }

            imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "NVAPI Auto-enable for Games");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Automatically enable NVAPI features for specific games.\n\n"
                    "Note: DLDSR needs to be off for proper functionality\n\n"
                    "Supported games:\n"
                    "- Armored Core 6\n"
                    "- Devil May Cry 5\n"
                    "- Elden Ring\n"
                    "- Hitman\n"
                    "- Resident Evil 2\n"
                    "- Resident Evil 3\n"
                    "- Resident Evil 7\n"
                    "- Resident Evil 8\n"
                    "- Sekiro: Shadows Die Twice");
            }

            // Display restart warning if needed
            if (s_restart_needed_nvapi.load()) {
                imgui.Spacing();
                imgui.TextColored(wrapper_colors::TEXT_ERROR, "Game restart required to apply NVAPI changes.");
            }
            if (::g_nvapiFullscreenPrevention.IsAvailable()) {
                // Library loaded successfully
                imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " NVAPI Library: Loaded");
            } else {
                // Library not loaded
                imgui.TextColored(wrapper_colors::ICON_ERROR, ICON_FK_CANCEL " NVAPI Library: Not Loaded");
            }
        }
        imgui.Unindent();
    }

    // Minimal NVIDIA Reflex Controls (device runtime dependent)
    if (imgui.CollapsingHeader("NVIDIA Reflex (Minimal)", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        // Native Reflex Status Indicator
        bool is_native_reflex_active = IsNativeReflexActive(now_ns);
        if (is_native_reflex_active) {
            imgui.TextColored(wrapper_colors::ICON_SUCCESS, ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("The game has native Reflex support and is actively using it. ");
            }
        } else {
            imgui.TextColored(wrapper_colors::TEXT_DIMMED,
                              ICON_FK_MINUS " Native Reflex: INACTIVE Limit Real Frames: OFF");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("No native Reflex activity detected. ");
            }
        }
        imgui.Spacing();

        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Enabling Reflex when the game already has it can cause conflicts, instability, or "
                "performance issues. Check the game's graphics settings first.");
        }

        bool reflex_auto_configure = settings::g_advancedTabSettings.reflex_auto_configure.GetValue();
        bool reflex_delay_first_500_frames = settings::g_advancedTabSettings.reflex_delay_first_500_frames.GetValue();
        bool reflex_use_markers = settings::g_advancedTabSettings.reflex_use_markers.GetValue();
        bool reflex_generate_markers = settings::g_advancedTabSettings.reflex_generate_markers.GetValue();
        bool reflex_enable_sleep = settings::g_advancedTabSettings.reflex_enable_sleep.GetValue();

        // Reflex enable / low latency / boost are derived from Main tab FPS limiter mode (onpresent_reflex_mode,
        // reflex_limiter_reflex_mode, reflex_disabled_limiter_mode). Shown as read-only Yes/No.
        const bool reflex_enabled = ShouldReflexBeEnabled();
        const bool reflex_low_latency = ShouldReflexLowLatencyBeEnabled();
        const bool reflex_boost = ShouldReflexBoostBeEnabled();
        imgui.Text("Reflex: %s", reflex_enabled ? "Yes" : "No");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Derived from Main tab FPS Limiter Mode and Reflex combo (OnPresent / Reflex / Disabled).");
        }
        imgui.Text("Low Latency: %s", reflex_low_latency ? "Yes" : "No");
        imgui.Text("Boost: %s", reflex_boost ? "Yes" : "No");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Configure in Main tab under FPS Limiter Mode (Reflex combo).");
        }

        if (imgui.Checkbox("Delay Reflex for first 500 frames", &reflex_delay_first_500_frames)) {
            settings::g_advancedTabSettings.reflex_delay_first_500_frames.SetValue(reflex_delay_first_500_frames);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "When enabled, NVIDIA Reflex integration will not be activated\n"
                "until after the first 500 frames of the game (g_global_frame_id >= 500),\n"
                "even if Reflex (from Main tab) or auto-configure would normally turn it on.");
        }

        if (imgui.Checkbox("Auto Configure Reflex", &reflex_auto_configure)) {
            settings::g_advancedTabSettings.reflex_auto_configure.SetValue(reflex_auto_configure);
            s_reflex_auto_configure.store(reflex_auto_configure);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip("Automatically configure Reflex settings on startup");
        }
        if (reflex_auto_configure) {
            imgui.Text("Auto-configure is handled by continuous monitoring");
        }
        if (reflex_enabled) {
            if (reflex_auto_configure) {
                imgui.BeginDisabled();
            }
            if (imgui.Checkbox("Use Reflex Markers", &reflex_use_markers)) {
                settings::g_advancedTabSettings.reflex_use_markers.SetValue(reflex_use_markers);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Tell NVIDIA Reflex to use markers for optimization");
            }

            if (imgui.Checkbox("Generate Reflex Markers", &reflex_generate_markers)) {
                settings::g_advancedTabSettings.reflex_generate_markers.SetValue(reflex_generate_markers);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Generate markers in the frame timeline for latency measurement");
            }
            // Warning about enabling Reflex when game already has it
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_generate_markers.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(wrapper_colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Generate Reflex Markers' if the game already has built-in "
                                  "Reflex support!");
            }

            if (imgui.Checkbox("Enable Reflex Sleep Mode", &reflex_enable_sleep)) {
                settings::g_advancedTabSettings.reflex_enable_sleep.SetValue(reflex_enable_sleep);
            }
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_enable_sleep.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(wrapper_colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Enable Reflex Sleep Mode' if the game already has "
                                  "built-in Reflex support!");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Enable Reflex sleep mode calls (disabled by default for safety).");
            }
            if (reflex_auto_configure) {
                imgui.EndDisabled();
            }
            bool reflex_logging = settings::g_advancedTabSettings.reflex_logging.GetValue();
            if (imgui.Checkbox("Enable Reflex Logging", &reflex_logging)) {
                settings::g_advancedTabSettings.reflex_logging.SetValue(reflex_logging);
                s_enable_reflex_logging.store(reflex_logging);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Enable detailed logging of Reflex marker operations for debugging purposes.");
            }
        }

        // Reflex Sleep Status Section
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        if (imgui.CollapsingHeader("Reflex Sleep Status", wrapper_flags::TreeNodeFlags_None)) {
            // Try to get sleep status from latency manager
            NV_GET_SLEEP_STATUS_PARAMS sleep_status = {};
            sleep_status.version = NV_GET_SLEEP_STATUS_PARAMS_VER;

            bool status_available = false;
            SleepStatusUnavailableReason unavailable_reason = SleepStatusUnavailableReason::kNone;

            if (!g_latencyManager) {
                unavailable_reason = SleepStatusUnavailableReason::kNoLatencyManager;
            } else if (!g_latencyManager->IsInitialized()) {
                unavailable_reason = SleepStatusUnavailableReason::kLatencyManagerNotInitialized;
            } else {
                status_available = g_latencyManager->GetSleepStatus(&sleep_status, &unavailable_reason);
            }

            if (status_available) {
                imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "Current Reflex Status:");
                imgui.Indent();

                // Low Latency Mode status
                bool low_latency_enabled = (sleep_status.bLowLatencyMode == NV_TRUE);
                imgui.TextColored(low_latency_enabled ? wrapper_colors::ICON_SUCCESS : wrapper_colors::TEXT_DIMMED,
                                  "Low Latency Mode: %s", low_latency_enabled ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "Indicates whether NVIDIA Reflex Low Latency Mode is currently active in the driver.");
                }

                // Fullscreen VRR status
                bool fs_vrr = (sleep_status.bFsVrr == NV_TRUE);
                imgui.Text("Fullscreen VRR: %s", fs_vrr ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "Indicates if fullscreen GSYNC or GSYNC Compatible mode is active (valid only when app is in "
                        "foreground).");
                }

                // Control Panel VSYNC Override
                bool cpl_vsync_on = (sleep_status.bCplVsyncOn == NV_TRUE);
                imgui.Text("Control Panel VSYNC Override: %s", cpl_vsync_on ? "ON" : "OFF");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Indicates if NVIDIA Control Panel is overriding VSYNC settings.");
                }

                // Sleep interval
                if (sleep_status.sleepIntervalUs > 0) {
                    float fps_limit = 1000000.0f / static_cast<float>(sleep_status.sleepIntervalUs);
                    imgui.Text("Sleep Interval: %u us (%.2f FPS limit)", sleep_status.sleepIntervalUs, fps_limit);
                } else {
                    imgui.Text("Sleep Interval: Not set");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Latest sleep interval in microseconds (inverse of FPS limit).");
                }

                // Game Sleep status
                bool use_game_sleep = (sleep_status.bUseGameSleep == NV_TRUE);
                imgui.Text("Game Sleep Calls: %s", use_game_sleep ? "ACTIVE" : "INACTIVE");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Indicates if NvAPI_D3D_Sleep() is being called by the game or addon.");
                }

                imgui.Unindent();
            } else {
                imgui.TextColored(wrapper_colors::TEXT_DIMMED, "Sleep status not available: %s",
                                  SleepStatusUnavailableReasonToString(unavailable_reason));
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "Sleep status requires an initialized DirectX 11/12 device and NVIDIA GPU with Reflex "
                        "support.");
                }
            }

            // NvLL VK (Vulkan Reflex) params when NvLowLatencyVk hooks are active
            if (AreNvLowLatencyVkHooksInstalled()) {
                imgui.Spacing();
                imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "NvLL VK (Vulkan Reflex) SetSleepMode:");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip(
                        "When NvLowLatencyVk hooks are installed, we re-apply SleepMode on SIMULATION_START.\n"
                        "'Last applied' is what we sent to the driver; 'Game tried to set' is what the game passed.");
                }
                imgui.Indent();
                NvLLVkSleepModeParamsView last_applied = {};
                GetNvLowLatencyVkLastAppliedSleepModeParams(&last_applied);
                if (last_applied.has_value) {
                    imgui.TextColored(wrapper_colors::ICON_SUCCESS, "Last applied (via SetSleepMode_Original):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               last_applied.low_latency ? "Yes" : "No", last_applied.boost ? "Yes" : "No",
                               last_applied.minimum_interval_us);
                    if (last_applied.minimum_interval_us > 0) {
                        float fps = 1000000.0f / last_applied.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImGuiWrapperColor{0.6f, 0.6f, 0.6f, 1.0f}, "Last applied: (none yet)");
                }
                NvLLVkSleepModeParamsView game_params = {};
                GetNvLowLatencyVkGameSleepModeParams(&game_params);
                if (game_params.has_value) {
                    imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f},
                                      "Game tried to set (NvLL_VK_SetSleepMode):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               game_params.low_latency ? "Yes" : "No", game_params.boost ? "Yes" : "No",
                               game_params.minimum_interval_us);
                    if (game_params.minimum_interval_us > 0) {
                        float fps = 1000000.0f / game_params.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImGuiWrapperColor{0.6f, 0.6f, 0.6f, 1.0f}, "Game tried to set: (none yet)");
                }
                imgui.Unindent();
            }
        }

        // Reflex Debug Counters Section
        imgui.Spacing();
        imgui.Separator();
        imgui.Spacing();

        if (imgui.CollapsingHeader("Reflex Debug Counters", wrapper_flags::TreeNodeFlags_None)) {
            extern std::atomic<uint32_t> g_reflex_sleep_count;
            extern std::atomic<uint32_t> g_reflex_apply_sleep_mode_count;
            extern std::atomic<LONGLONG> g_reflex_sleep_duration_ns;
            extern std::atomic<uint32_t> g_reflex_marker_simulation_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_simulation_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_rendersubmit_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_present_start_count;
            extern std::atomic<uint32_t> g_reflex_marker_present_end_count;
            extern std::atomic<uint32_t> g_reflex_marker_input_sample_count;

            uint32_t sleep_count = ::g_reflex_sleep_count.load();
            uint32_t apply_sleep_mode_count = ::g_reflex_apply_sleep_mode_count.load();
            LONGLONG sleep_duration_ns = ::g_reflex_sleep_duration_ns.load();
            uint32_t sim_start_count = ::g_reflex_marker_simulation_start_count.load();
            uint32_t sim_end_count = ::g_reflex_marker_simulation_end_count.load();
            uint32_t render_start_count = ::g_reflex_marker_rendersubmit_start_count.load();
            uint32_t render_end_count = ::g_reflex_marker_rendersubmit_end_count.load();
            uint32_t present_start_count = ::g_reflex_marker_present_start_count.load();
            uint32_t present_end_count = ::g_reflex_marker_present_end_count.load();
            uint32_t input_sample_count = ::g_reflex_marker_input_sample_count.load();

            uint32_t total_marker_count = sim_start_count + sim_end_count + render_start_count + render_end_count
                                          + present_start_count + present_end_count + input_sample_count;

            imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "Reflex API Call Counters:");
            imgui.Indent();
            imgui.Text("Sleep calls: %u", sleep_count);
            if (sleep_count > 0) {
                double sleep_duration_ms = sleep_duration_ns / 1000000.0;
                imgui.Text("Avg Sleep Duration: %.3f ms", sleep_duration_ms);
            }
            imgui.Text("ApplySleepMode calls: %u", apply_sleep_mode_count);
            imgui.Text("Total SetMarker calls: %u", total_marker_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "Individual Marker Type Counts:");
            imgui.Indent();
            imgui.Text("SIMULATION_START: %u", sim_start_count);
            imgui.Text("SIMULATION_END: %u", sim_end_count);
            imgui.Text("RENDERSUBMIT_START: %u", render_start_count);
            imgui.Text("RENDERSUBMIT_END: %u", render_end_count);
            imgui.Text("PRESENT_START: %u", present_start_count);
            imgui.Text("PRESENT_END: %u", present_end_count);
            imgui.Text("INPUT_SAMPLE: %u", input_sample_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImGuiWrapperColor{0.6f, 0.6f, 0.6f, 1.0f},
                              "These counters help debug Reflex FPS limiter issues.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Marker counts show which specific markers are being set:\n"
                    "- SIMULATION_START/END: Frame simulation markers\n"
                    "- RENDERSUBMIT_START/END: GPU submission markers\n"
                    "- PRESENT_START/END: Present call markers\n"
                    "- INPUT_SAMPLE: Input sampling markers\n\n"
                    "If all marker counts are 0, Reflex markers are not being set.\n"
                    "If Sleep calls are 0, the Reflex sleep mode is not being called.\n"
                    "If ApplySleepMode calls are 0, the Reflex configuration is not being applied.");
            }

            imgui.Spacing();
            imgui.Separator();
            imgui.Spacing();

            // Native Reflex Counters
            uint32_t native_sleep_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].load();
            uint32_t native_set_sleep_mode_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].load();
            uint32_t native_set_latency_marker_count =
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].load();
            uint32_t native_get_latency_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].load();
            uint32_t native_get_sleep_status_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_SLEEP_STATUS].load();
            LONGLONG native_sleep_ns = ::g_sleep_reflex_native_ns.load();
            LONGLONG native_sleep_ns_smooth = ::g_sleep_reflex_native_ns_smooth.load();

            imgui.TextColored(ImGuiWrapperColor{0.8f, 0.8f, 0.8f, 1.0f}, "Native Reflex API Call Counters:");
            imgui.Indent();
            imgui.Text("NvAPI_D3D_Sleep calls: %u", native_sleep_count);
            if (native_sleep_count > 0 && native_sleep_ns_smooth > 0) {
                double native_calls_per_second = 1000000000.0 / static_cast<double>(native_sleep_ns_smooth);
                imgui.Text("Native Sleep Rate: %.2f times/sec (%.1f ms interval)", native_calls_per_second,
                           native_sleep_ns_smooth / 1000000.0);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms",
                                     native_sleep_ns > 0 ? native_sleep_ns / 1000000.0 : 0.0);
                }
            }
            imgui.Text("NvAPI_D3D_SetSleepMode calls: %u", native_set_sleep_mode_count);
            imgui.Text("NvAPI_D3D_SetLatencyMarker calls: %u", native_set_latency_marker_count);
            imgui.Text("NvAPI_D3D_GetLatency calls: %u", native_get_latency_count);
            imgui.Text("NvAPI_D3D_GetSleepStatus calls: %u", native_get_sleep_status_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImGuiWrapperColor{0.6f, 0.6f, 0.6f, 1.0f},
                              "These counters track native Reflex API calls from the game.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip(
                    "Native Reflex counters show when the game itself calls NVAPI Reflex functions:\n"
                    "- NvAPI_D3D_Sleep: Game's sleep calls for frame pacing\n"
                    "- NvAPI_D3D_SetSleepMode: Game's Reflex configuration calls\n"
                    "- NvAPI_D3D_SetLatencyMarker: Game's latency marker calls\n"
                    "- NvAPI_D3D_GetLatency: Game's latency query calls\n"
                    "- NvAPI_D3D_GetSleepStatus: Game's sleep status query calls\n\n"
                    "If all counts are 0, the game is not using native Reflex.\n"
                    "If counts are increasing, the game has native Reflex support.");
            }

            if (imgui.Button("Reset Counters")) {
                // Reset injected Reflex counters
                ::g_reflex_sleep_count.store(0);
                ::g_reflex_apply_sleep_mode_count.store(0);
                ::g_reflex_sleep_duration_ns.store(0);
                ::g_reflex_marker_simulation_start_count.store(0);
                ::g_reflex_marker_simulation_end_count.store(0);
                ::g_reflex_marker_rendersubmit_start_count.store(0);
                ::g_reflex_marker_rendersubmit_end_count.store(0);
                ::g_reflex_marker_present_start_count.store(0);
                ::g_reflex_marker_present_end_count.store(0);
                ::g_reflex_marker_input_sample_count.store(0);

                // Reset native Reflex counters
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].store(0);
                ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_SLEEP_STATUS].store(0);
                ::g_sleep_reflex_native_ns.store(0);
                ::g_sleep_reflex_native_ns_smooth.store(0);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltip("Reset all Reflex debug counters to zero.");
            }
        }
        imgui.Unindent();
    }

    // Fake NVAPI Settings
    imgui.Spacing();
    if (imgui.CollapsingHeader("AntiLag 2 / XeLL support (fakenvapi / custom nvapi64.dll)",
                               wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        imgui.TextColored(wrapper_colors::TEXT_WARNING, "Load AL2/AL+/XeLL through nvapi64.dll");

        bool fake_nvapi_enabled = settings::g_advancedTabSettings.fake_nvapi_enabled.GetValue();
        if (imgui.Checkbox("Enable (requires restart)", &fake_nvapi_enabled)) {
            settings::g_advancedTabSettings.fake_nvapi_enabled.SetValue(fake_nvapi_enabled);
            settings::g_advancedTabSettings.fake_nvapi_enabled.Save();
            s_restart_needed_nvapi.store(true);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "AntiLag 2, Vulkan AntiLag+ or XeLL are automatically selected when available.\n"
                "Add nvapi64.dll to the addon directory (rename fakenvapi.dll if needed).\n\n"
                "Downlaod from here: https://github.com/emoose/fakenvapi\n");
        }

        imgui.Unindent();  // Unindent nested header section

        // Warning about experimental nature
        imgui.Spacing();
        imgui.TextColored(wrapper_colors::TEXT_WARNING, ICON_FK_WARNING " Experimental Feature");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltip(
                "Fake NVAPI is experimental and may cause:\n"
                "- Game crashes or instability\n"
                "- Performance issues\n"
                "- Incompatibility with some games\n\n"
                "Use at your own risk!");
        }
        imgui.Unindent();
    }
}

void DrawNewExperimentalFeatures(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Warning tip
    imgui.TextColored(wrapper_colors::TEXT_WARNING, ICON_FK_WARNING " Tip: Turn off if this causes crashes");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltip(
            "These experimental features are under active development.\n"
            "If you experience crashes or instability, disable them immediately.");
    }

    imgui.Spacing();

    imgui.Unindent();
}

}  // namespace ui::new_ui
