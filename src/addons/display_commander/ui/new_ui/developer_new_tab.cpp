#include "developer_new_tab.hpp"
#include "../../globals.hpp"
#include "../../nvapi/fake_nvapi_manager.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../presentmon/presentmon_manager.hpp"
#include "../../res/forkawesome.h"
#include "../../res/ui_colors.hpp"
#include "../../settings/developer_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/reshade_global_config.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/process_window_enumerator.hpp"
#include "imgui.h"
#include "settings_wrapper.hpp"


#include <atomic>
#include <set>
#include <algorithm>
#include <string>
#include <cstring>

#include <dxgi1_6.h>
#include <wrl/client.h>

// External atomic variables from settings
extern std::atomic<bool> s_nvapi_auto_enable_enabled;

namespace ui::new_ui {

void InitDeveloperNewTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        // Settings already loaded at startup

        // Apply LoadFromDllMain setting to ReShade on startup
        utils::SetLoadFromDllMain(settings::g_developerTabSettings.load_from_dll_main.GetValue());

        settings_loaded = true;
    }
}

void DrawDeveloperNewTab() {
    if (ImGui::CollapsingHeader("Features Enabled By Default", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawFeaturesEnabledByDefault();
    }
    ImGui::Spacing();

    // Developer Settings Section
    if (ImGui::CollapsingHeader("Developer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawDeveloperSettings();
    }

    ImGui::Spacing();

    // HDR and Display Settings Section
    if (ImGui::CollapsingHeader("HDR and Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawHdrDisplaySettings();
    }

    ImGui::Spacing();

    // NVAPI Settings Section - only show if game is in NVAPI game list
    DrawNvapiSettings();

    ImGui::Spacing();

    // New Experimental Features Section
    if (ImGui::CollapsingHeader("New Experimental Features", ImGuiTreeNodeFlags_None)) {
        DrawNewExperimentalFeatures();
    }

    ImGui::Spacing();

    // Debug Tools Section
    if (ImGui::CollapsingHeader("Debug Tools", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();

        if (ImGui::Button(ICON_FK_FILE " Log All Processes & Windows")) {
            LogInfo("Button clicked: Starting process and window enumeration...");
            display_commander::utils::LogAllProcessesAndWindows();
            LogInfo("Button handler: Process and window enumeration function returned");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Enumerates all running processes and their windows, logging detailed information to the log file.\n"
                             "Useful for debugging overlay detection and window management issues.");
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::Separator();
}

void DrawFeaturesEnabledByDefault() {
    ImGui::Indent();

    // Prevent Fullscreen
    CheckboxSetting(settings::g_developerTabSettings.prevent_fullscreen, "Prevent Fullscreen");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevent exclusive fullscreen; keep borderless/windowed for stability and HDR.");
    }

    CheckboxSetting(settings::g_developerTabSettings.prevent_always_on_top, "Prevent Always On Top");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevents windows from becoming always on top, even if they are moved or resized.");
    }
    #if 0
    // LoadFromDllMain setting
    if (CheckboxSetting(settings::g_developerTabSettings.load_from_dll_main, "LoadFromDllMain (ReShade) (requires restart)")) {
        LogInfo("LoadFromDllMain setting changed to: %s",
                settings::g_developerTabSettings.load_from_dll_main.GetValue() ? "enabled" : "disabled");
        // Apply the setting to ReShade immediately
        utils::SetLoadFromDllMain(settings::g_developerTabSettings.load_from_dll_main.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Sets LoadFromDllMain=1 in ReShade configuration.\n"
            "This setting controls how ReShade loads and initializes.\n"
            "When enabled, ReShade will load from DllMain instead of the normal loading process.\n"
            "This setting requires a game restart to take effect.");
    }
    #endif
    #if 0

    // Load Streamline setting
    if (CheckboxSetting(settings::g_developerTabSettings.load_streamline, "Hook Streamline SDK (sl.interposer.dll)")) {
        LogInfo("Load Streamline setting changed to: %s",
                settings::g_developerTabSettings.load_streamline.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls whether to load and hook into sl.interposer.dll (Streamline SDK).\n"
            "When enabled, Display Commander will install hooks for Streamline functions.\n"
            "This setting is automatically disabled when safemode is enabled.\n"
            "This setting requires a game restart to take effect.");
    }

    // Load _nvngx setting
    if (CheckboxSetting(settings::g_developerTabSettings.load_nvngx, "Hook NVIDIA NGX SDK (_nvngx.dll)")) {
        LogInfo("Load _nvngx setting changed to: %s",
                settings::g_developerTabSettings.load_nvngx.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls whether to load and hook into _nvngx.dll (NVIDIA NGX SDK).\n"
            "When enabled, Display Commander will install hooks for NGX functions.\n"
            "This setting is automatically disabled when safemode is enabled.\n"
            "This setting requires a game restart to take effect.");
    }

    // Load nvapi64 setting
    if (CheckboxSetting(settings::g_developerTabSettings.load_nvapi64, "Hook NVIDIA API (nvapi64.dll)")) {
        LogInfo("Load nvapi64 setting changed to: %s",
                settings::g_developerTabSettings.load_nvapi64.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Controls whether to load and hook into nvapi64.dll (NVIDIA API).\n"
            "When enabled, Display Commander will install hooks for NVAPI functions.\n"
            "This setting is automatically disabled when safemode is enabled.\n"
            "This setting requires a game restart to take effect.");
    }


    ImGui::Spacing();
    #endif

    ImGui::Unindent();
}

void DrawDeveloperSettings() {
    ImGui::Indent();

    // Safemode setting
    if (CheckboxSetting(settings::g_developerTabSettings.safemode, "Safemode (requires restart)")) {
        LogInfo("Safemode setting changed to: %s",
                settings::g_developerTabSettings.safemode.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Safemode disables all auto-apply settings and sets FPS limiter to disabled.\n"
            "When enabled, it will automatically set itself to 0 and disable:\n"
            "- Auto-apply resolution changes\n"
            "- Auto-apply refresh rate changes\n"
            "- Apply display settings at start\n"
            "- FPS limiter mode (set to disabled)\n\n"
            "This setting requires a game restart to take effect.");
    }

    // DLLs to load before Display Commander
    std::string dlls_to_load = settings::g_developerTabSettings.dlls_to_load_before.GetValue();
    char dlls_buffer[512] = {0};
    strncpy_s(dlls_buffer, sizeof(dlls_buffer), dlls_to_load.c_str(), _TRUNCATE);
    if (ImGui::InputText("DLLs to Load Before Display Commander", dlls_buffer, sizeof(dlls_buffer))) {
        settings::g_developerTabSettings.dlls_to_load_before.SetValue(std::string(dlls_buffer));
        LogInfo("DLLs to load before set to: %s", dlls_buffer);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Comma or semicolon-separated list of DLL names to wait for before Display Commander continues initialization.\n"
            "Example: dll1.dll, dll2.dll, dll3.dll or dll1.dll; dll2.dll; dll3.dll\n"
            "Display Commander will wait for each DLL to be loaded (up to 30 seconds per DLL) before proceeding.\n"
            "This happens before the DLL loading delay.\n\n"
            "This setting requires a game restart to take effect.");
    }

    // DLL loading delay setting
    int delay_ms = settings::g_developerTabSettings.dll_loading_delay_ms.GetValue();
    if (ImGui::SliderInt("DLL Loading Delay (ms)", &delay_ms, 0, 10000, delay_ms == 0 ? "No delay" : "%d ms")) {
        settings::g_developerTabSettings.dll_loading_delay_ms.SetValue(delay_ms);
        LogInfo("DLL loading delay set to %d ms", delay_ms);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Delay before installing LoadLibrary hooks (in milliseconds).\n"
            "This can help with compatibility issues by allowing other DLLs to load first.\n"
            "Set to 0 to disable delay.\n\n"
            "This setting requires a game restart to take effect.");
    }

    // Suppress MinHook setting
    if (CheckboxSetting(settings::g_developerTabSettings.suppress_minhook, "Suppress MinHook Initialization")) {
        LogInfo("Suppress MinHook setting changed to: %s",
                settings::g_developerTabSettings.suppress_minhook.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Suppress all MinHook initialization calls (MH_Initialize).\n"
            "When enabled, all hook functions will skip MinHook initialization.\n"
            "This can help with compatibility issues or debugging.\n"
            "This setting is automatically enabled when safemode is active.\n\n"
            "This setting requires a game restart to take effect.");
    }

    ImGui::Spacing();

    // Auto-hide Discord Overlay setting
    if (CheckboxSetting(settings::g_developerTabSettings.auto_hide_discord_overlay, "Auto-hide Discord Overlay")) {
        LogInfo("Auto-hide Discord Overlay setting changed to: %s",
                settings::g_developerTabSettings.auto_hide_discord_overlay.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatically hide Discord Overlay window when it overlaps with the game window.\n"
            "This prevents the overlay from interfering with MPO iFlip and can improve performance.\n"
            "Similar to Special-K's behavior when AllowWindowedMode=false.\n\n"
            "The check runs every second in the continuous monitoring thread.");
    }

    ImGui::Spacing();

    // Suppress Window Changes setting
    if (CheckboxSetting(settings::g_developerTabSettings.suppress_window_changes, "Suppress Window Changes")) {
        LogInfo("Suppress Window Changes setting changed to: %s",
                settings::g_developerTabSettings.suppress_window_changes.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Suppresses automatic window position, size, and style changes from continuous monitoring.\n"
            "When enabled, ApplyWindowChange will not be called automatically.\n"
            "This is a compatibility feature for cases where automatic window management causes issues.\n\n"
            "Default: disabled (window changes are applied automatically).");
    }

    ImGui::Spacing();

    // PresentMon ETW Tracing setting
    if (CheckboxSetting(settings::g_developerTabSettings.enable_presentmon_tracing, "Enable PresentMon ETW Tracing")) {
        LogInfo("PresentMon ETW tracing setting changed to: %s",
                settings::g_developerTabSettings.enable_presentmon_tracing.GetValue() ? "enabled" : "disabled");

        // Start or stop PresentMon based on setting
        if (settings::g_developerTabSettings.enable_presentmon_tracing.GetValue()) {
            presentmon::g_presentMonManager.StartWorker();
        } else {
            presentmon::g_presentMonManager.StopWorker();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
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
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " ACTIVE");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("PresentMon worker thread is currently running.");
        }

        // Show detailed debug info when active in developer tab
        ImGui::Indent();
        presentmon::PresentMonFlipState pm_flip_state;
        presentmon::PresentMonDebugInfo pm_debug_info;
        bool has_pm_flip_state = presentmon::g_presentMonManager.GetFlipState(pm_flip_state);
        presentmon::g_presentMonManager.GetDebugInfo(pm_debug_info);

        ImGui::TextColored(ui::colors::TEXT_LABEL, "ETW Status:");
        ImGui::SameLine();
        ImGui::Text("%s", pm_debug_info.etw_session_status.c_str());

        if (!pm_debug_info.last_error.empty()) {
            ImGui::TextColored(ui::colors::TEXT_ERROR, "Last Error: %s", pm_debug_info.last_error.c_str());
        }

        ImGui::TextColored(ui::colors::TEXT_LABEL, "Events:");
        ImGui::SameLine();
        ImGui::Text("%llu (pid=%llu)",
                    static_cast<unsigned long long>(pm_debug_info.events_processed),
                    static_cast<unsigned long long>(pm_debug_info.events_processed_for_current_pid));

        ImGui::TextColored(ui::colors::TEXT_LABEL, "Last Event PID:");
        ImGui::SameLine();
        ImGui::Text("%u", static_cast<unsigned int>(pm_debug_info.last_event_pid));

        ImGui::TextColored(ui::colors::TEXT_LABEL, "Providers:");
        ImGui::SameLine();
        ImGui::Text("DxgKrnl=%llu, DXGI=%llu, DWM=%llu",
                    static_cast<unsigned long long>(pm_debug_info.events_dxgkrnl),
                    static_cast<unsigned long long>(pm_debug_info.events_dxgi),
                    static_cast<unsigned long long>(pm_debug_info.events_dwm));

        if (!pm_debug_info.last_graphics_provider.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Last Graphics Event:");
            ImGui::SameLine();
            ImGui::Text("%s | id=%u | pid=%u",
                        pm_debug_info.last_graphics_provider.c_str(),
                        static_cast<unsigned int>(pm_debug_info.last_graphics_event_id),
                        static_cast<unsigned int>(pm_debug_info.last_graphics_event_pid));
        }
        if (!pm_debug_info.last_graphics_provider_name.empty() || !pm_debug_info.last_graphics_event_name.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Graphics Schema:");
            ImGui::SameLine();
            ImGui::Text("%s :: %s",
                        pm_debug_info.last_graphics_provider_name.empty() ? "(unknown provider)" : pm_debug_info.last_graphics_provider_name.c_str(),
                        pm_debug_info.last_graphics_event_name.empty() ? "(unknown event)" : pm_debug_info.last_graphics_event_name.c_str());
        }
        ImGui::TextColored(ui::colors::TEXT_LABEL, "Graphics Props:");
        ImGui::SameLine();
        if (!pm_debug_info.last_graphics_props.empty()) {
            ImGui::TextWrapped("%s", pm_debug_info.last_graphics_props.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "(none)");
        }

        if (!pm_debug_info.last_provider.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Last Event:");
            ImGui::SameLine();
            ImGui::Text("%s | id=%u", pm_debug_info.last_provider.c_str(), static_cast<unsigned int>(pm_debug_info.last_event_id));
        }
        if (!pm_debug_info.last_provider_name.empty() || !pm_debug_info.last_event_name.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Schema:");
            ImGui::SameLine();
            ImGui::Text("%s :: %s",
                        pm_debug_info.last_provider_name.empty() ? "(unknown provider)" : pm_debug_info.last_provider_name.c_str(),
                        pm_debug_info.last_event_name.empty() ? "(unknown event)" : pm_debug_info.last_event_name.c_str());
        }
        if (!pm_debug_info.last_interesting_props.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Props:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", pm_debug_info.last_interesting_props.c_str());
        }
        if (!pm_debug_info.last_present_mode_value.empty()) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Last PresentMode:");
            ImGui::SameLine();
            ImGui::Text("%s", pm_debug_info.last_present_mode_value.c_str());
        }

        if (has_pm_flip_state) {
            ImGui::TextColored(ui::colors::TEXT_LABEL, "Flip Mode:");
            ImGui::SameLine();
            ImGui::Text("%s", pm_flip_state.present_mode_str.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Flip Mode: (No data yet)");
        }

        // DWM Flip Compatibility (separate from flip-state)
        presentmon::PresentMonFlipCompatibility pm_flip_compat;
        if (presentmon::g_presentMonManager.GetFlipCompatibility(pm_flip_compat)) {
            ImGui::Spacing();
            if (ImGui::CollapsingHeader("Flip Compatibility (DWM)", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Indent();

                // Age
                LONGLONG now_ns = utils::get_now_ns();
                double age_ms =
                    static_cast<double>(now_ns - static_cast<LONGLONG>(pm_flip_compat.last_update_time_ns)) / 1000000.0;
                ImGui::TextColored(ui::colors::TEXT_DIMMED, "Last update: %.1f ms ago", age_ms);

                ImGui::Text("surfaceLuid: 0x%llx", static_cast<unsigned long long>(pm_flip_compat.surface_luid));
                ImGui::Text("Surface: %ux%u  PixelFormat=%u  ColorSpace=%u  Flags=0x%x",
                            pm_flip_compat.surface_width, pm_flip_compat.surface_height,
                            pm_flip_compat.pixel_format, pm_flip_compat.color_space, pm_flip_compat.flags);

                auto show_bool = [](const char* label, bool v) {
                    ImGui::Text("%s: %s", label, v ? "Yes" : "No");
                };

                show_bool("IsDirectFlipCompatible", pm_flip_compat.is_direct_flip_compatible);
                show_bool("IsAdvancedDirectFlipCompatible", pm_flip_compat.is_advanced_direct_flip_compatible);
                show_bool("IsOverlayCompatible", pm_flip_compat.is_overlay_compatible);
                show_bool("IsOverlayRequired", pm_flip_compat.is_overlay_required);
                show_bool("fNoOverlappingContent", pm_flip_compat.no_overlapping_content);

                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Recent surfaces (last 10s)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    std::vector<presentmon::PresentMonSurfaceCompatibilitySummary> surfaces;
                    presentmon::g_presentMonManager.GetRecentFlipCompatibilitySurfaces(surfaces, 10000);

                    ImGui::TextColored(ui::colors::TEXT_DIMMED, "Surfaces: %d", static_cast<int>(surfaces.size()));

                    if (ImGui::BeginTable("##pm_surfaces", 10,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit
                                              | ImGuiTableFlags_ScrollY,
                                          ImVec2(0, 260))) {
                        ImGui::TableSetupColumn("Age(ms)");
                        ImGui::TableSetupColumn("surfaceLuid");
                        ImGui::TableSetupColumn("hwnd");
                        ImGui::TableSetupColumn("WxH");
                        ImGui::TableSetupColumn("PF");
                        ImGui::TableSetupColumn("CS");
                        ImGui::TableSetupColumn("Flags");
                        ImGui::TableSetupColumn("Direct");
                        ImGui::TableSetupColumn("Overlay");
                        ImGui::TableSetupColumn("Count");
                        ImGui::TableHeadersRow();

                        for (const auto& s : surfaces) {
                            const double age_ms =
                                static_cast<double>(now_ns - static_cast<LONGLONG>(s.last_update_time_ns)) / 1000000.0;

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%.0f", age_ms);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("0x%llx", static_cast<unsigned long long>(s.surface_luid));

                            ImGui::TableSetColumnIndex(2);
                            if (s.hwnd != 0) {
                                ImGui::Text("0x%llx", static_cast<unsigned long long>(s.hwnd));
                            } else {
                                ImGui::TextColored(ui::colors::TEXT_DIMMED, "(unknown)");
                            }

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%ux%u", s.surface_width, s.surface_height);

                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text("%u", s.pixel_format);

                            ImGui::TableSetColumnIndex(5);
                            ImGui::Text("%u", s.color_space);

                            ImGui::TableSetColumnIndex(6);
                            ImGui::Text("0x%x", s.flags);

                            ImGui::TableSetColumnIndex(7);
                            ImGui::Text("%s%s",
                                        s.is_direct_flip_compatible ? "Y" : "N",
                                        s.is_advanced_direct_flip_compatible ? " (adv)" : "");

                            ImGui::TableSetColumnIndex(8);
                            ImGui::Text("%s%s",
                                        s.is_overlay_compatible ? "Y" : "N",
                                        s.is_overlay_required ? " (req)" : "");

                            ImGui::TableSetColumnIndex(9);
                            ImGui::Text("%llu", static_cast<unsigned long long>(s.count));
                        }

                        ImGui::EndTable();
                    }
                }

                ImGui::Unindent();
            }
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("ETW Event Type Explorer (Debug)", ImGuiTreeNodeFlags_None)) {
            static bool s_graphics_only = true;
            ImGui::Checkbox("Graphics-only (DxgKrnl/DXGI/DWM)", &s_graphics_only);

            std::vector<presentmon::PresentMonEventTypeSummary> types;
            presentmon::g_presentMonManager.GetEventTypeSummaries(types, s_graphics_only);

            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Cached event types: %d", static_cast<int>(types.size()));

            if (ImGui::BeginTable("##pm_event_types", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, 2220))) {
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Provider");
                ImGui::TableSetupColumn("EventId");
                ImGui::TableSetupColumn("Task");
                ImGui::TableSetupColumn("Op");
                ImGui::TableSetupColumn("Keyword");
                ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed, 600.0f);
                ImGui::TableHeadersRow();

                const int max_rows = 200;
                int rows = 0;
                for (const auto& t : types) {
                    if (rows++ >= max_rows) break;
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu", static_cast<unsigned long long>(t.count));

                    ImGui::TableSetColumnIndex(1);
                    if (!t.provider_name.empty()) {
                        ImGui::Text("%s", t.provider_name.c_str());
                    } else {
                        ImGui::Text("%s", t.provider_guid.c_str());
                    }
                    if (!t.event_name.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", t.event_name.c_str());
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", static_cast<unsigned int>(t.event_id));

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", static_cast<unsigned int>(t.task));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%u", static_cast<unsigned int>(t.opcode));

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("0x%llx", static_cast<unsigned long long>(t.keyword));

                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextWrapped("%s", t.props.empty() ? "(no schema/props)" : t.props.c_str());
                }

                ImGui::EndTable();
            }
        }

        ImGui::Unindent();
    }

    ImGui::Spacing();

    // Debug Layer checkbox with warning
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "REQUIRES SETUP:");
    ImGui::SameLine();
    if (CheckboxSetting(settings::g_developerTabSettings.debug_layer_enabled, "Enable DX11/DX12 Debug Layer")) {
        LogInfo("Debug layer setting changed to: %s",
                settings::g_developerTabSettings.debug_layer_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            ICON_FK_WARNING " WARNING: Debug Layer Setup Required " ICON_FK_WARNING "\n\n"
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
            "- Debug output appears in DbgView\n\n"
            ICON_FK_WARNING " May significantly impact performance when enabled!");
    }

    // Show status when debug layer is enabled
    if (settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " ACTIVE");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Debug layer is currently ENABLED.\n"
                "- Debug output should appear in DbgView\n"
                "- Performance may be significantly reduced\n"
                "- Restart game if you just enabled this setting\n"
                "- Disable when not debugging to restore performance");
        }
    }

    // SetBreakOnSeverity checkbox (only shown when debug layer is enabled)
    if (settings::g_developerTabSettings.debug_layer_enabled.GetValue()) {
        ImGui::Indent();
        if (CheckboxSetting(settings::g_developerTabSettings.debug_break_on_severity, "SetBreakOnSeverity (All Levels)")) {
            LogInfo("Debug break on severity setting changed to: %s",
                    settings::g_developerTabSettings.debug_break_on_severity.GetValue() ? "enabled" : "disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
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
        ImGui::Unindent();
    }

    ImGui::Unindent();
}

void DrawHdrDisplaySettings() {
    ImGui::Indent();

    // Hide HDR Capabilities
    if (CheckboxSetting(settings::g_developerTabSettings.hide_hdr_capabilities, "Hide game's native HDR")) {
        s_hide_hdr_capabilities.store(settings::g_developerTabSettings.hide_hdr_capabilities.GetValue());
        LogInfo("HDR hiding setting changed to: %s",
                settings::g_developerTabSettings.hide_hdr_capabilities.GetValue() ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Hides HDR capabilities from applications by intercepting CheckColorSpaceSupport and GetDesc calls.\n"
            "This can prevent games from detecting HDR support and force them to use SDR mode.");
    }

    // Enable Flip Chain
    if (CheckboxSetting(settings::g_developerTabSettings.enable_flip_chain, "Enable flip chain")) {
        s_enable_flip_chain.store(settings::g_developerTabSettings.enable_flip_chain.GetValue());
        LogInfo("Enable flip chain setting changed to: %s",
                settings::g_developerTabSettings.enable_flip_chain.GetValue() ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Forces games to use flip model swap chains (FLIP_DISCARD) for better performance.\n"
            "This setting requires a game restart to take effect.\n"
            "Only works with DirectX 10/11/12 (DXGI) games.");
    }

    // Auto Color Space checkbox
    bool auto_colorspace = settings::g_developerTabSettings.auto_colorspace.GetValue();
    if (ImGui::Checkbox("Auto color space", &auto_colorspace)) {
        settings::g_developerTabSettings.auto_colorspace.SetValue(auto_colorspace);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatically sets the appropriate color space on the game's swap chain based on the current format.\n"
            "- HDR10 format (R10G10B10A2) → HDR10 color space (ST2084)\n"
            "- FP16 format (R16G16B16A16) → scRGB color space (Linear)\n"
            "- SDR format (R8G8B8A8) → sRGB color space (Non-linear)\n"
            "Only works with DirectX 11/12 games.\n"
            "Applied automatically in presentBefore.");
    }




    // Show upgrade status
    if (s_d3d9e_upgrade_successful.load()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " D3D9 upgraded to D3D9Ex successfully");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Direct3D 9 was successfully upgraded to Direct3D 9Ex.\n"
                "Your game is now using the enhanced D3D9Ex API.");
        }
        ImGui::Unindent();
    } else if (settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Waiting for D3D9 device creation...");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The upgrade will occur when the game creates a Direct3D 9 device.\n"
                "If the game is not using D3D9, this setting has no effect.");
        }
        ImGui::Unindent();
    }

    ImGui::Unindent();
}

void DrawNvapiSettings() {
    uint64_t now_ns = utils::get_now_ns();


    if (IsGameInNvapiAutoEnableList(GetCurrentProcessName())) {
        if (ImGui::CollapsingHeader("NVAPI Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent();
            // NVAPI Auto-enable checkbox
            if (CheckboxSetting(settings::g_developerTabSettings.nvapi_auto_enable_enabled, "Enable NVAPI Auto-enable for Games")) {
                s_nvapi_auto_enable_enabled.store(settings::g_developerTabSettings.nvapi_auto_enable_enabled.GetValue());
                LogInfo("NVAPI Auto-enable setting changed to: %s",
                        settings::g_developerTabSettings.nvapi_auto_enable_enabled.GetValue() ? "true" : "false");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Automatically enable NVAPI features for supported games when they are launched.");
            }

            // Display current game status
            ImGui::Spacing();
            std::string gameStatus = GetNvapiAutoEnableGameStatus();
            bool isGameSupported = IsGameInNvapiAutoEnableList(GetCurrentProcessName());

            if (isGameSupported) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Current Game: %s", gameStatus.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This game is supported for NVAPI auto-enable features.");
                }
                // Warning about Alt+Enter requirement
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FK_WARNING " Warning: Requires pressing Alt+Enter once");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Press Alt-Enter to enable HDR.\n"
                        "This is required for proper HDR functionality.");
                }

            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FK_CANCEL " Current Game: %s", gameStatus.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This game is not in the NVAPI auto-enable supported games list.");
                }
            }

            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "NVAPI Auto-enable for Games");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
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
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply NVAPI changes.");
            }
            if (::g_nvapiFullscreenPrevention.IsAvailable()) {
                // Library loaded successfully
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " NVAPI Library: Loaded");
            } else {
                // Library not loaded
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ICON_FK_CANCEL " NVAPI Library: Not Loaded");
            }
        }
        ImGui::Unindent();
    }

    // Minimal NVIDIA Reflex Controls (device runtime dependent)
    if (ImGui::CollapsingHeader("NVIDIA Reflex (Minimal)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        // Native Reflex Status Indicator
        bool is_native_reflex_active = IsNativeReflexActive(now_ns);
        if (is_native_reflex_active) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "The game has native Reflex support and is actively using it. ");
            }
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), ICON_FK_MINUS " Native Reflex: INACTIVE Limit Real Frames: OFF");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "No native Reflex activity detected. ");
            }
        }
        ImGui::Spacing();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Enabling Reflex when the game already has it can cause conflicts, instability, or "
                "performance issues. Check the game's graphics settings first.");
        }

        bool reflex_auto_configure = settings::g_developerTabSettings.reflex_auto_configure.GetValue();
        bool reflex_enable = settings::g_developerTabSettings.reflex_enable.GetValue();
        bool reflex_delay_first_500_frames = settings::g_developerTabSettings.reflex_delay_first_500_frames.GetValue();

        bool reflex_low_latency = settings::g_developerTabSettings.reflex_low_latency.GetValue();
        bool reflex_boost = settings::g_developerTabSettings.reflex_boost.GetValue();
        bool reflex_use_markers = settings::g_developerTabSettings.reflex_use_markers.GetValue();
        bool reflex_generate_markers = settings::g_developerTabSettings.reflex_generate_markers.GetValue();
        bool reflex_enable_sleep = settings::g_developerTabSettings.reflex_enable_sleep.GetValue();


        if (ImGui::Checkbox("Delay Reflex for first 500 frames", &reflex_delay_first_500_frames)) {
            settings::g_developerTabSettings.reflex_delay_first_500_frames.SetValue(reflex_delay_first_500_frames);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, NVIDIA Reflex integration will not be activated\n"
                "until after the first 500 frames of the game (g_global_frame_id >= 500),\n"
                "even if 'Enable Reflex' or auto-configure would normally turn it on.");
        }

        if (ImGui::Checkbox("Auto Configure Reflex", &reflex_auto_configure)) {
            settings::g_developerTabSettings.reflex_auto_configure.SetValue(reflex_auto_configure);
            s_reflex_auto_configure.store(reflex_auto_configure);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically configure Reflex settings on startup");
        }
        if (ImGui::Checkbox("Enable Reflex", &reflex_enable)) {
            settings::g_developerTabSettings.reflex_enable.SetValue(reflex_enable);
        }
        if (reflex_auto_configure) {
            ImGui::EndDisabled();
            ImGui::Text("Auto-configure is handled by continuous monitoring");
        }
        if (reflex_enable) {
            if (ImGui::Checkbox("Low Latency Mode", &reflex_low_latency)) {
                settings::g_developerTabSettings.reflex_low_latency.SetValue(reflex_low_latency);
            }
            if (ImGui::Checkbox("Boost", &reflex_boost)) {
                settings::g_developerTabSettings.reflex_boost.SetValue(reflex_boost);
            }
            if (reflex_auto_configure) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Checkbox("Use Reflex Markers", &reflex_use_markers)) {
                settings::g_developerTabSettings.reflex_use_markers.SetValue(reflex_use_markers);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Tell NVIDIA Reflex to use markers for optimization");
            }

            if (ImGui::Checkbox("Generate Reflex Markers", &reflex_generate_markers)) {
                settings::g_developerTabSettings.reflex_generate_markers.SetValue(reflex_generate_markers);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Generate markers in the frame timeline for latency measurement");
            }
            // Warning about enabling Reflex when game already has it
            if (is_native_reflex_active && settings::g_developerTabSettings.reflex_generate_markers.GetValue()) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING
                    " Warning: Do not enable 'Generate Reflex Markers' if the game already has built-in Reflex support!");
            }

            if (ImGui::Checkbox("Enable Reflex Sleep Mode", &reflex_enable_sleep)) {
                settings::g_developerTabSettings.reflex_enable_sleep.SetValue(reflex_enable_sleep);
            }
            if (is_native_reflex_active && settings::g_developerTabSettings.reflex_enable_sleep.GetValue()) {
                ImGui::SameLine();
                ImGui::TextColored(
                    ImVec4(1.0f, 0.6f, 0.0f, 1.0f), ICON_FK_WARNING
                    " Warning: Do not enable 'Enable Reflex Sleep Mode' if the game already has built-in Reflex support!");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable Reflex sleep mode calls (disabled by default for safety).");
            }
            if (reflex_auto_configure) {
                ImGui::EndDisabled();
            }
            bool reflex_logging = settings::g_developerTabSettings.reflex_logging.GetValue();
            if (ImGui::Checkbox("Enable Reflex Logging", &reflex_logging)) {
                settings::g_developerTabSettings.reflex_logging.SetValue(reflex_logging);
                s_enable_reflex_logging.store(reflex_logging);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable detailed logging of Reflex marker operations for debugging purposes.");
            }
        }

        // Reflex Debug Counters Section
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Reflex Debug Counters", ImGuiTreeNodeFlags_DefaultOpen)) {
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

            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Reflex API Call Counters:");
            ImGui::Indent();
            ImGui::Text("Sleep calls: %u", sleep_count);
            if (sleep_count > 0) {
                double sleep_duration_ms = sleep_duration_ns / 1000000.0;
                ImGui::Text("Avg Sleep Duration: %.3f ms", sleep_duration_ms);
            }
            ImGui::Text("ApplySleepMode calls: %u", apply_sleep_mode_count);
            ImGui::Text("Total SetMarker calls: %u", total_marker_count);
            ImGui::Unindent();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Individual Marker Type Counts:");
            ImGui::Indent();
            ImGui::Text("SIMULATION_START: %u", sim_start_count);
            ImGui::Text("SIMULATION_END: %u", sim_end_count);
            ImGui::Text("RENDERSUBMIT_START: %u", render_start_count);
            ImGui::Text("RENDERSUBMIT_END: %u", render_end_count);
            ImGui::Text("PRESENT_START: %u", present_start_count);
            ImGui::Text("PRESENT_END: %u", present_end_count);
            ImGui::Text("INPUT_SAMPLE: %u", input_sample_count);
            ImGui::Unindent();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "These counters help debug Reflex FPS limiter issues.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Marker counts show which specific markers are being set:\n"
                    "- SIMULATION_START/END: Frame simulation markers\n"
                    "- RENDERSUBMIT_START/END: GPU submission markers\n"
                    "- PRESENT_START/END: Present call markers\n"
                    "- INPUT_SAMPLE: Input sampling markers\n\n"
                    "If all marker counts are 0, Reflex markers are not being set.\n"
                    "If Sleep calls are 0, the Reflex sleep mode is not being called.\n"
                    "If ApplySleepMode calls are 0, the Reflex configuration is not being applied.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Native Reflex Counters
            uint32_t native_sleep_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SLEEP].load();
            uint32_t native_set_sleep_mode_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_SLEEP_MODE].load();
            uint32_t native_set_latency_marker_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_SET_LATENCY_MARKER].load();
            uint32_t native_get_latency_count = ::g_nvapi_event_counters[NVAPI_EVENT_D3D_GET_LATENCY].load();
            LONGLONG native_sleep_ns = ::g_sleep_reflex_native_ns.load();
            LONGLONG native_sleep_ns_smooth = ::g_sleep_reflex_native_ns_smooth.load();

            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Native Reflex API Call Counters:");
            ImGui::Indent();
            ImGui::Text("NvAPI_D3D_Sleep calls: %u", native_sleep_count);
            if (native_sleep_count > 0 && native_sleep_ns_smooth > 0) {
                double native_calls_per_second = 1000000000.0 / static_cast<double>(native_sleep_ns_smooth);
                ImGui::Text("Native Sleep Rate: %.2f times/sec (%.1f ms interval)",
                           native_calls_per_second, native_sleep_ns_smooth / 1000000.0);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Smoothed interval using rolling average. Raw: %.1f ms",
                                    native_sleep_ns > 0 ? native_sleep_ns / 1000000.0 : 0.0);
                }
            }
            ImGui::Text("NvAPI_D3D_SetSleepMode calls: %u", native_set_sleep_mode_count);
            ImGui::Text("NvAPI_D3D_SetLatencyMarker calls: %u", native_set_latency_marker_count);
            ImGui::Text("NvAPI_D3D_GetLatency calls: %u", native_get_latency_count);
            ImGui::Unindent();

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "These counters track native Reflex API calls from the game.");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Native Reflex counters show when the game itself calls NVAPI Reflex functions:\n"
                    "- NvAPI_D3D_Sleep: Game's sleep calls for frame pacing\n"
                    "- NvAPI_D3D_SetSleepMode: Game's Reflex configuration calls\n"
                    "- NvAPI_D3D_SetLatencyMarker: Game's latency marker calls\n"
                    "- NvAPI_D3D_GetLatency: Game's latency query calls\n\n"
                    "If all counts are 0, the game is not using native Reflex.\n"
                    "If counts are increasing, the game has native Reflex support.");
            }

            if (ImGui::Button("Reset Counters")) {
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
                ::g_sleep_reflex_native_ns.store(0);
                ::g_sleep_reflex_native_ns_smooth.store(0);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset all Reflex debug counters to zero.");
            }
        }
        ImGui::Unindent();
    }

    // Fake NVAPI Settings
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("AntiLag 2 / XeLL support (fakenvapi / custom nvapi64.dll)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextColored(ui::colors::TEXT_WARNING, "Load AL2/AL+/XeLL through nvapi64.dll");

        bool fake_nvapi_enabled = settings::g_developerTabSettings.fake_nvapi_enabled.GetValue();
        if (ImGui::Checkbox("Enable (requires restart)", &fake_nvapi_enabled)) {
            settings::g_developerTabSettings.fake_nvapi_enabled.SetValue(fake_nvapi_enabled);
            settings::g_developerTabSettings.fake_nvapi_enabled.Save();
            s_restart_needed_nvapi.store(true);
        }
         if (ImGui::IsItemHovered()) {
             ImGui::SetTooltip(
                "AntiLag 2, Vulkan AntiLag+ or XeLL are automatically selected when available.\n"
                "Add nvapi64.dll to the addon directory (rename fakenvapi.dll if needed).\n\n"
                "Downlaod from here: https://github.com/emoose/fakenvapi\n"
             );
         }

        // Fake NVAPI Status
        auto stats = nvapi::g_fakeNvapiManager.GetStatistics();
        std::string status_msg = nvapi::g_fakeNvapiManager.GetStatusMessage();

        // Show warning if fakenvapi.dll is found (needs renaming)
        if (fake_nvapi_enabled && stats.fakenvapi_dll_found) {
            ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Warning: fakenvapi.dll found - rename to nvapi64.dll");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "fakenvapi.dll was found in the addon directory.\n"
                    "For newer optiscaler builds, rename fakenvapi.dll to nvapi64.dll\n"
                    "to ensure proper functionality.");
            }
        }

        if (stats.is_nvapi64_loaded && !stats.fake_nvapi_loaded) {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Status: nvapi64.dll was auto-loaded by the game.");
        } else if (stats.fake_nvapi_loaded) {
            ImGui::TextColored(ui::colors::TEXT_SUCCESS, "Status: nvapi64.dll was loaded by DC from local directory.");
        } else if (!stats.last_error.empty()) {
            ImGui::TextColored(ui::colors::TEXT_ERROR, "Status: %s", stats.last_error.c_str());
        } else {
            ImGui::TextColored(ui::colors::TEXT_DIMMED, "Status: %s", status_msg.c_str());
        }

    // Statistics (see docs/UI_STYLE_GUIDE.md for depth/indent rules)
    // Depth 2: Nested subsection with indentation and distinct colors
    ImGui::Indent();  // Indent nested header
    ui::colors::PushNestedHeaderColors();  // Apply distinct colors for nested header
    if (ImGui::CollapsingHeader("Fake NVAPI Statistics", ImGuiTreeNodeFlags_None)) {
        ImGui::Indent();  // Indent content inside subsection
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "nvapi64.dll loaded before DC: %s", stats.was_nvapi64_loaded_before_dc ? "Yes" : "No");
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "nvapi64.dll currently loaded: %s", stats.is_nvapi64_loaded ? "Yes" : "No");
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "libxell.dll loaded: %s", stats.is_libxell_loaded ? "Yes" : "No");
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Fake NVAPI Loaded: %s", stats.fake_nvapi_loaded ? "Yes" : "No");
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Override Enabled: %s", stats.override_enabled ? "Yes" : "No");

        if (stats.fakenvapi_dll_found) {
            ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING ": fakenvapi.dll found: Yes (needs renaming to nvapi64.dll)");
        } else {
            ImGui::TextColored(ui::colors::TEXT_DEFAULT, "fakenvapi.dll found: No");
        }

            if (!stats.last_error.empty()) {
            ImGui::TextColored(ui::colors::TEXT_ERROR, "Last Error: %s", stats.last_error.c_str());
            }
        ImGui::Unindent();  // Unindent content
        }
    ui::colors::PopNestedHeaderColors();  // Restore default header colors
    ImGui::Unindent();  // Unindent nested header section

        // Warning about experimental nature
        ImGui::Spacing();
        ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Experimental Feature");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Fake NVAPI is experimental and may cause:\n"
                "- Game crashes or instability\n"
                "- Performance issues\n"
                "- Incompatibility with some games\n\n"
                "Use at your own risk!");
        }
        ImGui::Unindent();
    }

}


void DrawNewExperimentalFeatures() {
    ImGui::Indent();

    // Warning tip
    ImGui::TextColored(ui::colors::TEXT_WARNING, ICON_FK_WARNING " Tip: Turn off if this causes crashes");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("These experimental features are under active development.\n"
                         "If you experience crashes or instability, disable them immediately.");
    }

    ImGui::Spacing();

    // Reuse swap chain experimental feature
    if (CheckboxSetting(settings::g_experimentalTabSettings.reuse_swap_chain_experimental_enabled, "Reuse Swap Chain")) {
        LogInfo("Reuse swap chain experimental feature %s",
                settings::g_experimentalTabSettings.reuse_swap_chain_experimental_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Store a global reference to the DXGI swapchain.\n"
                         "This allows other parts of the codebase to access the swapchain.\n"
                         "WARNING: Experimental feature - may cause crashes. Turn off if issues occur.");
    }

    // Display current swapchain pointer if available
    IDXGISwapChain* current_swapchain = global_dxgi_swapchain.load(std::memory_order_acquire);
    if (current_swapchain != nullptr) {
        ImGui::TextColored(ui::colors::TEXT_DEFAULT, "Current swapchain: 0x%p", current_swapchain);
    } else {
        ImGui::TextColored(ui::colors::TEXT_DIMMED, "No swapchain stored");
    }

    ImGui::Unindent();
}



}  // namespace ui::new_ui
