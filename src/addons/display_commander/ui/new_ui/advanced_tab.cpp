#include "advanced_tab.hpp"
#include "../../display/dpi_management.hpp"
#include "../../globals.hpp"
#include "../../hooks/vulkan/nvlowlatencyvk_hooks.hpp"
#include "../../latency/reflex_provider.hpp"
#include "../../latent_sync/refresh_rate_monitor_integration.hpp"
#include "../../presentmon/presentmon_manager.hpp"
#include "../forkawesome.h"
#include "../ui_colors.hpp"
#include "../../settings/advanced_tab_settings.hpp"
#include "../../settings/experimental_tab_settings.hpp"
#include "../../swapchain_events.hpp"
#include "../../ui/imgui_wrapper_base.hpp"
#include "../../utils/general_utils.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/timing.hpp"
#include "settings_wrapper.hpp"

#include <atomic>
#include <string>
#include <vector>

#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace ui::new_ui {

using namespace display_commander::ui;

void DrawFeaturesEnabledByDefault(display_commander::ui::IImGuiWrapper& imgui);
void DrawAdvancedTabSettingsSection(display_commander::ui::GraphicsApi api,
                                    display_commander::ui::IImGuiWrapper& imgui);
void DrawGlobalSettingsSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawPresentMonSection(display_commander::ui::IImGuiWrapper& imgui);
void DrawHdrDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);
void DrawNvapiSettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui);
void DrawDxgiRefreshRateSection(display_commander::ui::IImGuiWrapper& imgui);

void InitAdvancedTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        // Settings already loaded at startup
        settings_loaded = true;
    }
}

void DrawAdvancedTab(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    // Global settings (stored in Display Commander folder, shared across all games)
    if (imgui.CollapsingHeader("Global settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawGlobalSettingsSection(imgui);
    }

    if (imgui.CollapsingHeader("Features Enabled By Default", wrapper_flags::TreeNodeFlags_None)) {
        DrawFeaturesEnabledByDefault(imgui);
    }
    // Advanced Settings Section
    if (imgui.CollapsingHeader("Advanced Settings", wrapper_flags::TreeNodeFlags_None)) {
        DrawAdvancedTabSettingsSection(api, imgui);
    }

    // DXGI refresh rate subsection (enable + debug stats)
    if (imgui.CollapsingHeader("DXGI refresh rate", wrapper_flags::TreeNodeFlags_None)) {
        DrawDxgiRefreshRateSection(imgui);
    }

    // PresentMon ETW Tracing Section (requires EXPERIMENTAL_FEATURES)
    if (presentmon::kPresentMonEnabled
        && imgui.CollapsingHeader("PresentMon ETW Tracing", wrapper_flags::TreeNodeFlags_None)) {
        DrawPresentMonSection(imgui);
    }
    // NVAPI Settings Section - only show if game is in NVAPI game list
    DrawNvapiSettings(api, imgui);

}

void DrawFeaturesEnabledByDefault(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    CheckboxSetting(settings::g_advancedTabSettings.flush_command_queue_before_sleep,
                    "Flush command queue before FPS limiter sleep", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled (default), DX11/DX12 present path flushes the command queue before any FPS limiter sleep.\n"
            "Reduces input-to-display latency when the limiter is active. Disable only if you observe issues.");
    }

    CheckboxSetting(settings::g_advancedTabSettings.enqueue_gpu_completion,
                    "Enqueue GPU completion (from present-update)", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled (default), enqueues GPU completion measurement from the last present-update state "
            "(DX11/DX12).\n"
            "Used for latency and GPU timing. Disable only if you observe issues or want to reduce overhead.");
    }
    if (::g_smooth_motion_dll_loaded.load(std::memory_order_relaxed)) {
        imgui.SameLine();
        imgui.TextColored(::ui::colors::TEXT_WARNING, "(Disabled due to Smooth Motion)");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "nvpresent DLL is loaded; GPU completion measurement is suppressed while Smooth Motion is active.");
        }
    }

    imgui.Unindent();
}

void DrawPresentMonSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    if (CheckboxSetting(settings::g_advancedTabSettings.enable_presentmon_tracing, "Enable PresentMon ETW Tracing",
                        imgui)) {
        LogInfo("PresentMon ETW tracing setting changed to: %s",
                settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue() ? "enabled" : "disabled");

        if (!settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue()) {
            presentmon::StopAndDestroyPresentMon(presentmon::PresentMonStopReason::UserDisabled);
        }
        // When enabled, the continuous monitoring thread starts the worker on its next loop.
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Enable PresentMon ETW (Event Tracing for Windows) tracing for presentation tracking.\n"
            "Similar to Special-K's PresentMon integration.\n\n"
            "FEATURES:\n"
            "- Tracks presentation timing and frame pacing\n"
            "- Provides latency and flip information\n"
            "- Useful for VRR indicator on D3D12 games\n"
            "- Required for accurate presentation stats on non-NVIDIA hardware\n\n"
            "STATUS:\n"
            "- Worker is started by the continuous monitoring thread (not from the UI thread)\n"
            "- Flip mode is best-effort (depends on ETW provider fields)\n"
            "- Default: enabled\n\n"
            "Note: Requires appropriate Windows permissions for ETW tracing.");
    }

    imgui.Spacing();
    imgui.TextColored(::ui::colors::TEXT_LABEL, "ETW providers (apply on next start):");
    imgui.Indent();
    CheckboxSetting(settings::g_advancedTabSettings.presentmon_provider_dxgkrnl, "Subscribe to DxgKrnl", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Microsoft-Windows-DxgKrnl. Kernel graphics events.");
    }
    CheckboxSetting(settings::g_advancedTabSettings.presentmon_provider_dxgi, "Subscribe to DXGI", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Microsoft-Windows-DXGI. Swapchain and present events.");
    }
    CheckboxSetting(settings::g_advancedTabSettings.presentmon_provider_dwm, "Subscribe to DWM", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Microsoft-Windows-Dwm-Core. Desktop Window Manager composition.");
    }
    CheckboxSetting(settings::g_advancedTabSettings.presentmon_provider_d3d9, "Subscribe to D3D9", imgui);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Microsoft-Windows-D3D9. Direct3D 9 present events (per-draw counting).");
    }
    imgui.Unindent();
    imgui.Spacing();

    if (presentmon::g_presentMonManager.IsRunning()) {
        imgui.SameLine();
        imgui.TextColored(::ui::colors::ICON_SUCCESS, ICON_FK_OK " ACTIVE");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("PresentMon worker thread is currently running.");
        }

        imgui.Indent();
        presentmon::PresentMonFlipState pm_flip_state;
        presentmon::PresentMonDebugInfo pm_debug_info;
        bool has_pm_flip_state = presentmon::g_presentMonManager.GetFlipState(pm_flip_state);
        presentmon::g_presentMonManager.GetDebugInfo(pm_debug_info);

        imgui.TextColored(::ui::colors::TEXT_LABEL, "ETW Status:");
        imgui.SameLine();
        if (!pm_debug_info.etw_session_name.empty()) {
            imgui.Text("%s [%s]", pm_debug_info.etw_session_status.c_str(), pm_debug_info.etw_session_name.c_str());
        } else {
            imgui.Text("%s", pm_debug_info.etw_session_status.c_str());
        }

        if (!pm_debug_info.etw_enumeration_error.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "DC_ ETW Sessions:");
            imgui.SameLine();
            imgui.TextColored(::ui::colors::TEXT_ERROR, "%s", pm_debug_info.etw_enumeration_error.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Could not enumerate ETW sessions. List is empty. Try running as administrator or check "
                    "permissions.");
            }
        } else if (!pm_debug_info.dc_etw_sessions.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL,
                              "DC_ ETW Sessions (%zu):", pm_debug_info.dc_etw_sessions.size());
            imgui.Indent();
            for (const auto& session_name : pm_debug_info.dc_etw_sessions) {
                imgui.PushID(session_name.c_str());

                bool is_current_session = (session_name == pm_debug_info.etw_session_name);

                imgui.Text("  • %s", session_name.c_str());
                imgui.SameLine();

                if (is_current_session) {
                    imgui.BeginDisabled();
                }

                imgui.PushStyleColor(4, ImVec4{0.7f, 0.2f, 0.2f, 0.6f});
                imgui.PushStyleColor(5, ImVec4{0.9f, 0.3f, 0.3f, 0.8f});
                imgui.PushStyleColor(6, ::ui::colors::TEXT_ERROR);

                if (imgui.SmallButton(ICON_FK_CANCEL)) {
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
                        imgui.SetTooltipEx("Cannot stop current session");
                    }
                } else {
                    if (imgui.IsItemHovered()) {
                        imgui.SetTooltipEx("Stop ETW session: %s", session_name.c_str());
                    }
                }

                imgui.PopID();
            }
            imgui.Unindent();
        } else {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "DC_ ETW Sessions:");
            imgui.SameLine();
            imgui.TextColored(::ui::colors::TEXT_LABEL, "(waiting for data...)");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Enumeration succeeded; no DC_ sessions found yet. Our session may appear shortly.");
            }
        }

        if (!pm_debug_info.last_error.empty()) {
            imgui.TextColored(::ui::colors::TEXT_ERROR, "Last Error: %s", pm_debug_info.last_error.c_str());
        }

        imgui.TextColored(::ui::colors::TEXT_LABEL, "Events:");
        imgui.SameLine();
        imgui.Text("%llu (pid=%llu)", static_cast<unsigned long long>(pm_debug_info.events_processed),
                   static_cast<unsigned long long>(pm_debug_info.events_processed_for_current_pid));

        imgui.TextColored(::ui::colors::TEXT_LABEL, "Last Event PID:");
        imgui.SameLine();
        imgui.Text("%u", static_cast<unsigned int>(pm_debug_info.last_event_pid));

        imgui.TextColored(::ui::colors::TEXT_LABEL, "Providers:");
        imgui.SameLine();
        imgui.Text("DxgKrnl=%llu, DXGI=%llu, DWM=%llu, D3D9=%llu",
                   static_cast<unsigned long long>(pm_debug_info.events_dxgkrnl),
                   static_cast<unsigned long long>(pm_debug_info.events_dxgi),
                   static_cast<unsigned long long>(pm_debug_info.events_dwm),
                   static_cast<unsigned long long>(pm_debug_info.events_d3d9));

        if (!pm_debug_info.last_graphics_provider.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Last Graphics Event:");
            imgui.SameLine();
            imgui.Text("%s | id=%u | pid=%u", pm_debug_info.last_graphics_provider.c_str(),
                       static_cast<unsigned int>(pm_debug_info.last_graphics_event_id),
                       static_cast<unsigned int>(pm_debug_info.last_graphics_event_pid));
        }
        if (!pm_debug_info.last_graphics_provider_name.empty() || !pm_debug_info.last_graphics_event_name.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Graphics Schema:");
            imgui.SameLine();
            imgui.Text("%s :: %s",
                       pm_debug_info.last_graphics_provider_name.empty()
                           ? "(unknown provider)"
                           : pm_debug_info.last_graphics_provider_name.c_str(),
                       pm_debug_info.last_graphics_event_name.empty() ? "(unknown event)"
                                                                      : pm_debug_info.last_graphics_event_name.c_str());
        }
        imgui.TextColored(::ui::colors::TEXT_LABEL, "Graphics Props:");
        imgui.SameLine();
        if (!pm_debug_info.last_graphics_props.empty()) {
            imgui.TextWrapped("%s", pm_debug_info.last_graphics_props.c_str());
        } else {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "(none)");
        }

        if (!pm_debug_info.last_provider.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Last Event:");
            imgui.SameLine();
            imgui.Text("%s | id=%u", pm_debug_info.last_provider.c_str(),
                       static_cast<unsigned int>(pm_debug_info.last_event_id));
        }
        if (!pm_debug_info.last_provider_name.empty() || !pm_debug_info.last_event_name.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Schema:");
            imgui.SameLine();
            imgui.Text(
                "%s :: %s",
                pm_debug_info.last_provider_name.empty() ? "(unknown provider)"
                                                         : pm_debug_info.last_provider_name.c_str(),
                pm_debug_info.last_event_name.empty() ? "(unknown event)" : pm_debug_info.last_event_name.c_str());
        }
        if (!pm_debug_info.last_interesting_props.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Props:");
            imgui.SameLine();
            imgui.TextWrapped("%s", pm_debug_info.last_interesting_props.c_str());
        }
        if (!pm_debug_info.last_present_mode_value.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Last PresentMode:");
            imgui.SameLine();
            imgui.Text("%s", pm_debug_info.last_present_mode_value.c_str());
        }

        if (has_pm_flip_state) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Flip Mode:");
            imgui.SameLine();
            imgui.Text("%s", pm_flip_state.present_mode_str.c_str());
        } else {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "Flip Mode: (No data yet)");
        }

        presentmon::PresentMonFlipCompatibility pm_flip_compat;
        if (presentmon::g_presentMonManager.GetFlipCompatibility(pm_flip_compat)) {
            imgui.Spacing();
            if (imgui.CollapsingHeader("Flip Compatibility (DWM)", wrapper_flags::TreeNodeFlags_DefaultOpen)) {
                imgui.Indent();

                LONGLONG now_ns = utils::get_now_ns();
                double age_ms =
                    static_cast<double>(now_ns - static_cast<LONGLONG>(pm_flip_compat.last_update_time_ns)) / 1000000.0;
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "Last update: %.1f ms ago", age_ms);

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

                    imgui.TextColored(::ui::colors::TEXT_DIMMED, "Surfaces: %d", static_cast<int>(surfaces.size()));

                    if (imgui.BeginTable("##pm_surfaces", 10,
                                         wrapper_flags::TableFlags_RowBg | wrapper_flags::TableFlags_Borders
                                             | wrapper_flags::TableFlags_SizingFixedFit
                                             | wrapper_flags::TableFlags_ScrollY,
                                         ImVec2{0.f, 260.f})) {
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
                                imgui.TextColored(::ui::colors::TEXT_DIMMED, "(unknown)");
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

            imgui.TextColored(::ui::colors::TEXT_DIMMED, "Cached event types: %d", static_cast<int>(types.size()));

            if (imgui.BeginTable("##pm_event_types", 7,
                                 wrapper_flags::TableFlags_RowBg | wrapper_flags::TableFlags_Borders
                                     | wrapper_flags::TableFlags_SizingFixedFit | wrapper_flags::TableFlags_ScrollY,
                                 ImVec2{0.f, 2220.f})) {
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
                    if (imgui.IsItemHovered()) {
                        imgui.BeginTooltip();
                        if (!t.event_name.empty()) {
                            imgui.Text("%s", t.event_name.c_str());
                        }
                        if (!t.props_sample.empty()) {
                            if (!t.event_name.empty()) imgui.Separator();
                            imgui.TextColored(::ui::colors::TEXT_DIMMED, "Sample (one event):");
                            imgui.TextWrapped("%s", t.props_sample.c_str());
                        }
                        imgui.EndTooltip();
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
                    if (imgui.IsItemHovered() && !t.props_sample.empty()) {
                        imgui.BeginTooltip();
                        imgui.TextColored(::ui::colors::TEXT_DIMMED, "Sample (one event):");
                        imgui.TextWrapped("%s", t.props_sample.c_str());
                        imgui.EndTooltip();
                    }
                }

                imgui.EndTable();
            }
        }

        imgui.Unindent();
    } else {
        presentmon::PresentMonDebugInfo pm_debug_info_stopped;
        presentmon::g_presentMonManager.GetDebugInfo(pm_debug_info_stopped);

        imgui.SameLine();
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "ETW Tracing Status: Stopped");
        if (imgui.IsItemHovered()) {
            const bool setting_enabled = settings::g_advancedTabSettings.enable_presentmon_tracing.GetValue();
            std::string tip;
            if (setting_enabled) {
                tip = "PresentMon is enabled but the worker is not running.\n\n";
                tip += "Last known state:\n";
                tip += "  Thread status: ";
                tip += pm_debug_info_stopped.thread_status.empty() ? "(unknown)" : pm_debug_info_stopped.thread_status;
                tip += "\n  ETW status: ";
                tip += pm_debug_info_stopped.etw_session_status.empty() ? "(unknown)"
                                                                        : pm_debug_info_stopped.etw_session_status;
                tip +=
                    "\n\nPossible reasons:\n"
                    "- Worker failed to start or exited (e.g. StartTrace failed)\n"
                    "- ETW session could not be created\n"
                    "- Insufficient permissions (try running as administrator)\n";
                if (!pm_debug_info_stopped.last_error.empty()) {
                    tip += "\nLast error: ";
                    tip += pm_debug_info_stopped.last_error;
                }
                if (!pm_debug_info_stopped.etw_enumeration_error.empty()) {
                    tip += "\nSession enumeration: ";
                    tip += pm_debug_info_stopped.etw_enumeration_error;
                }
                if (pm_debug_info_stopped.last_error.empty() && pm_debug_info_stopped.etw_enumeration_error.empty()) {
                    tip +=
                        "\n\nNo error or enumeration failure recorded. Worker may have exited normally, "
                        "was stopped by user, or exited before reporting an error. Check ReShade log for [PresentMon] "
                        "messages.";
                }
            } else {
                tip = "Enable the checkbox above to start ETW tracing and see session list.";
            }
            imgui.SetTooltipEx("%s", tip.c_str());
        }
        imgui.Indent();
        // Always show last known thread/ETW status when stopped so user sees why even when last_error is empty
        imgui.TextColored(::ui::colors::TEXT_LABEL, "Thread status:");
        imgui.SameLine();
        imgui.TextColored(
            ::ui::colors::TEXT_DIMMED, "%s",
            pm_debug_info_stopped.thread_status.empty() ? "(unknown)" : pm_debug_info_stopped.thread_status.c_str());
        imgui.TextColored(::ui::colors::TEXT_LABEL, "ETW status:");
        imgui.SameLine();
        imgui.TextColored(::ui::colors::TEXT_DIMMED, "%s",
                          pm_debug_info_stopped.etw_session_status.empty()
                              ? "(unknown)"
                              : pm_debug_info_stopped.etw_session_status.c_str());
        if (!pm_debug_info_stopped.last_error.empty()) {
            imgui.TextColored(::ui::colors::TEXT_ERROR, "Last error: %s", pm_debug_info_stopped.last_error.c_str());
        }
        if (!pm_debug_info_stopped.etw_enumeration_error.empty()) {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "ETW sessions:");
            imgui.SameLine();
            imgui.TextColored(::ui::colors::TEXT_ERROR, "%s", pm_debug_info_stopped.etw_enumeration_error.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Could not enumerate ETW sessions. Try running as administrator.");
            }
        }
        imgui.Unindent();
    }

    // Subheader (closed by default): list all ETW sessions from all apps with option to stop any
    if (imgui.CollapsingHeader("Show all sessions from all apps", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        std::string all_err;
        std::vector<std::string> all_sessions;
        presentmon::PresentMonManager::GetEtwSessionsWithPrefix(L"", all_sessions, &all_err);
        if (!all_err.empty()) {
            imgui.TextColored(::ui::colors::TEXT_ERROR, "%s", all_err.c_str());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Try running as administrator to enumerate ETW sessions.");
            }
        } else if (all_sessions.empty()) {
            imgui.TextColored(::ui::colors::TEXT_DIMMED, "(no sessions)");
        } else {
            imgui.TextColored(::ui::colors::TEXT_LABEL, "Sessions (%zu):", all_sessions.size());
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
                    "Stopping a session may affect the app that created it. DC_ sessions are from Display Commander.");
            }
            imgui.Indent();
            for (const auto& session_name : all_sessions) {
                imgui.PushID(session_name.c_str());
                imgui.Text("  %s", session_name.empty() ? "(unnamed)" : session_name.c_str());
                imgui.SameLine();
                imgui.PushStyleColor(4, ImVec4{0.7f, 0.2f, 0.2f, 0.6f});
                imgui.PushStyleColor(5, ImVec4{0.9f, 0.3f, 0.3f, 0.8f});
                imgui.PushStyleColor(6, ::ui::colors::TEXT_ERROR);
                if (imgui.SmallButton(ICON_FK_CANCEL " Stop")) {
                    int wide_len = MultiByteToWideChar(CP_UTF8, 0, session_name.c_str(), -1, nullptr, 0);
                    if (wide_len > 0 && !session_name.empty()) {
                        std::vector<wchar_t> wide_name(static_cast<size_t>(wide_len));
                        MultiByteToWideChar(CP_UTF8, 0, session_name.c_str(), -1, wide_name.data(), wide_len);
                        presentmon::PresentMonManager::StopEtwSessionByName(wide_name.data());
                        LogInfo("Stopped ETW session: %s", session_name.c_str());
                    }
                }
                imgui.PopStyleColor(3);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Stop ETW session: %s", session_name.c_str());
                }
                imgui.PopID();
            }
            imgui.Unindent();
        }
        imgui.Unindent();
    }

    imgui.Unindent();
}

void DrawDcServiceStatusIndicators(display_commander::ui::IImGuiWrapper& imgui, bool include_version_in_tooltip) {
    (void)imgui;
    (void)include_version_in_tooltip;
}

void DrawDcServiceIndicatorsOnLine(display_commander::ui::IImGuiWrapper& imgui, bool include_version_in_tooltip) {
    (void)imgui;
    (void)include_version_in_tooltip;
}

void DrawDcServiceSection(display_commander::ui::IImGuiWrapper& imgui) {
    (void)imgui;
}

void DrawGlobalSettingsSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Auto-enable ReShade config backup for all games (stored in global_overrides.toml; overrides per-game value)
    if (CheckboxSetting(settings::g_advancedTabSettings.auto_enable_reshade_config_backup,
                        "Auto-enable ReShade config backup", imgui)) {
        if (settings::g_advancedTabSettings.auto_enable_reshade_config_backup.GetValue()) {
            CopyGameIniFilesToReshadeConfigBackupFolder();
        }
        LogInfo("Auto-enable ReShade config backup changed to: %s",
                settings::g_advancedTabSettings.auto_enable_reshade_config_backup.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, ReShade config backup is effectively on for all games (same as the per-game "
            "\"Auto ReShade config backup\" on the Main tab, but applied globally). Stored in the Display Commander "
            "folder (global_overrides.toml). Overrides the per-game value even when the game config has it.");
    }

    // Windows Gaming Input suppression globally (stored in global_overrides.toml; overrides per-game value)
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_wgi_globally,
                        "Enable Windows Gaming Input suppression globally", imgui)) {
        LogInfo("Suppress WGI globally changed to: %s",
                settings::g_advancedTabSettings.suppress_wgi_globally.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, Windows Gaming Input is suppressed for all games (same effect as the per-game checkbox "
            "in the Controller tab, but applied everywhere). Stored in the Display Commander folder "
            "(global_overrides.toml, same location as hotkeys.toml). Overrides the per-game value even when the game config has it. "
            "Restart each game to apply.");
    }
    imgui.SameLine();
    imgui.TextColored(::ui::colors::TEXT_WARNING, ICON_FK_WARNING);
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Suppressing Windows Gaming Input may break networking in some games.");
    }

    imgui.Unindent();
}

void DrawAdvancedTabSettingsSection(display_commander::ui::GraphicsApi api,
                                    display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Safemode setting
    if (CheckboxSetting(settings::g_advancedTabSettings.safemode, "Safemode (requires restart)", imgui)) {
        LogInfo("Safemode setting changed to: %s",
                settings::g_advancedTabSettings.safemode.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
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
        imgui.SetTooltipEx(
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
        imgui.SetTooltipEx(
            "Delay before installing LoadLibrary hooks (in milliseconds).\n"
            "This can help with compatibility issues by allowing other DLLs to load first.\n"
            "Set to 0 to disable delay.\n\n"
            "This setting requires a game restart to take effect.");
    }

    // Suppress MinHook setting
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_minhook, "Suppress MinHook Initialization", imgui)) {
        LogInfo("Suppress MinHook setting changed to: %s",
                settings::g_advancedTabSettings.suppress_minhook.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Suppress all MinHook initialization calls (MH_Initialize).\n"
            "When enabled, all hook functions will skip MinHook initialization.\n"
            "This can help with compatibility issues or debugging.\n"
            "This setting is automatically enabled when safemode is active.\n\n"
            "This setting requires a game restart to take effect.");
    }

    imgui.Spacing();

    // Block GameOverlayRenderer (Steam overlay) setting
    if (CheckboxSetting(settings::g_advancedTabSettings.block_gameoverlayrenderer,
                        "Block GameOverlayRenderer (Steam overlay DLL)", imgui)) {
        LogInfo("Block GameOverlayRenderer setting changed to: %s",
                settings::g_advancedTabSettings.block_gameoverlayrenderer.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Prevent gameoverlayrenderer.dll / gameoverlayrenderer64.dll (Steam overlay) from loading.\n"
            "When enabled, any attempt to load the Steam overlay DLL will fail with access denied.\n"
            "Use this if the overlay causes conflicts with ReShade or display settings.\n\n"
            "Takes effect on next load attempt (e.g. after game restart if Steam injects later).");
    }
    {
        HMODULE hSteamOverlay = GetModuleHandleW(L"gameoverlayrenderer64.dll");
        if (!hSteamOverlay) {
            hSteamOverlay = GetModuleHandleW(L"gameoverlayrenderer.dll");
        }
        imgui.TextColored(ui::colors::TEXT_DIMMED, "GameOverlayRenderer: %s", hSteamOverlay ? "Loaded" : "Not loaded");
    }

    imgui.Spacing();

    // Suppress Window Changes setting
    if (CheckboxSetting(settings::g_advancedTabSettings.suppress_window_changes, "Suppress Window Changes", imgui)) {
        LogInfo("Suppress Window Changes setting changed to: %s",
                settings::g_advancedTabSettings.suppress_window_changes.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
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
            imgui.SetTooltipEx(
                "For borderless windows: how long after the game loses focus Win+Up (restore) still works.\n"
                "0 = only when game is in foreground; 1-60 = seconds; 61 = Forever (Win+Up always works).\n"
                "Stored in Display Commander config (global). Default: 1 s.");
        }
    }

    imgui.Spacing();

    // Debug Layer checkbox with warning
    imgui.TextColored(::ui::colors::ICON_WARNING, ICON_FK_WARNING);
    imgui.SameLine();
    imgui.TextColored(::ui::colors::ICON_WARNING, "REQUIRES SETUP:");
    imgui.SameLine();
    if (CheckboxSetting(settings::g_advancedTabSettings.debug_layer_enabled, "Enable DX11/DX12 Debug Layer", imgui)) {
        LogInfo("Debug layer setting changed to: %s",
                settings::g_advancedTabSettings.debug_layer_enabled.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(ICON_FK_WARNING
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
        imgui.TextColored(::ui::colors::ICON_SUCCESS, ICON_FK_OK " ACTIVE");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
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
                            imgui)) {
            LogInfo("Debug break on severity setting changed to: %s",
                    settings::g_advancedTabSettings.debug_break_on_severity.GetValue() ? "enabled" : "disabled");
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
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

void DrawDxgiRefreshRateSection(display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    if (CheckboxSetting(settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection,
                        "DXGI refresh rate / VRR detection", imgui)) {
        LogInfo(
            "DXGI refresh rate / VRR detection setting changed to: %s",
            settings::g_advancedTabSettings.enable_dxgi_refresh_rate_vrr_detection.GetValue() ? "enabled" : "disabled");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "When enabled, DXGI Present detours signal the refresh rate monitor after each Present.\n"
            "This allows DXGI-based refresh rate and VRR detection (RefreshRateMonitor thread).\n"
            "When disabled, SignalRefreshRateMonitor is not called, reducing per-frame work.\n\n"
            "Default: disabled.");
    }

    imgui.Spacing();
    imgui.TextUnformatted("Debug stats:");
    imgui.Indent();

    const bool thread_running = dxgi::fps_limiter::IsRefreshRateMonitorThreadRunning();
    imgui.Text("Monitor thread: %s", thread_running ? "Running" : "Stopped");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("RefreshRateMonitor monitoring thread (started with StartRefreshRateMonitoring).");
    }

    const uint64_t signal_count = dxgi::fps_limiter::GetRefreshRateMonitorSignalCount();
    imgui.Text("Signals: %llu", static_cast<unsigned long long>(signal_count));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Number of times SignalRefreshRateMonitor was called from DXGI Present detours. "
            "Increments each frame when DXGI refresh rate / VRR detection is enabled. If this stays 0, Present hooks "
            "are not calling the signal.");
    }

    const uint64_t loop_count = dxgi::fps_limiter::GetRefreshRateMonitorLoopCount();
    imgui.Text("Loops: %llu", static_cast<unsigned long long>(loop_count));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Number of times the monitor thread loop ran (each wait + process iteration). "
            "Includes timeouts. Resets when monitoring is (re)started. If Loops stays 0, thread may not be running.");
    }

    const bool has_swap = dxgi::fps_limiter::RefreshRateMonitorHasSwapChain();
    imgui.Text("Swap chain: %s", has_swap ? "set" : "not set");
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Whether a swap chain was stored via SignalPresent (from Present detours). If not set, GetFrameStatistics "
            "is never called.");
    }

    const uint64_t frame_stats_tried = dxgi::fps_limiter::GetRefreshRateMonitorFrameStatsTried();
    const uint64_t frame_stats_ok = dxgi::fps_limiter::GetRefreshRateMonitorFrameStatsOk();
    imgui.Text("GetFrameStatistics: tried %llu, OK %llu", static_cast<unsigned long long>(frame_stats_tried),
               static_cast<unsigned long long>(frame_stats_ok));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Times IDXGISwapChain::GetFrameStatistics was called (tried) and returned success (OK). "
            "If tried=0, swap chain was never set when the loop ran. If tried>0 but OK=0, GetFrameStatistics is "
            "failing (e.g. DXGI_ERROR_FRAME_STATISTICS_DISJOINT).");
    }

    const HRESULT last_hr = dxgi::fps_limiter::GetRefreshRateMonitorLastFrameStatisticsHr();
    if (last_hr != 0) {
        imgui.TextColored(::ui::colors::TEXT_ERROR, "Last GetFrameStatistics HRESULT: 0x%08X",
                          static_cast<unsigned>(last_hr));
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Last failure code from IDXGISwapChain::GetFrameStatistics. "
                "Common: 0x887A0007 = DXGI_ERROR_FRAME_STATISTICS_DISJOINT (reset/transition). Also logged to file "
                "(first + every 100th failure).");
        }
    }

    const uint64_t skipped_no_diff = dxgi::fps_limiter::GetRefreshRateMonitorProcessSkippedNoDiff();
    imgui.Text("Skipped (no diff): %llu", static_cast<unsigned long long>(skipped_no_diff));
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Times ProcessFrameStatistics got stats but skipped because SyncRefreshCount difference was <= 0 (no new "
            "present since last check).");
    }

    const long long last_ns = dxgi::fps_limiter::GetRefreshRateMonitorLastStatsTimeNs();
    if (last_ns == 0) {
        imgui.Text("Last stats: never");
    } else {
        const LONGLONG now_ns = utils::get_now_ns();
        const double ago_sec = static_cast<double>(now_ns - last_ns) / static_cast<double>(utils::SEC_TO_NS);
        imgui.Text("Last stats: %.2f s ago", ago_sec);
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx("Last time GetFrameStatistics was successfully processed (ProcessFrameStatistics).");
    }

    dxgi::fps_limiter::RefreshRateStats stats = dxgi::fps_limiter::GetRefreshRateStats();
    imgui.Text("Status: %s", stats.status.c_str());
    imgui.Text("Samples: %u", stats.sample_count);
    imgui.Text("Current: %.2f Hz | Min: %.2f Hz | Max: %.2f Hz", stats.current_rate, stats.min_rate, stats.max_rate);

    imgui.Unindent();
    imgui.Unindent();
}

void DrawHdrDisplaySettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    imgui.Indent();

    // Hide HDR Capabilities
    if (CheckboxSetting(settings::g_advancedTabSettings.hide_hdr_capabilities,
                        "Hide display's HDR capabilities from game", imgui)) {
        LogInfo("HDR hiding setting changed to: %s",
                settings::g_advancedTabSettings.hide_hdr_capabilities.GetValue() ? "true" : "false");
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Tries to prevent the game from turning on its HDR.\n"
            "Hides HDR capabilities from the game by intercepting CheckColorSpaceSupport and GetDesc calls,\n"
            "so the game may use SDR mode instead.");
    }

    // Disable DPI Scaling checkbox
    if (CheckboxSetting(settings::g_advancedTabSettings.disable_dpi_scaling, "Disable DPI scaling", imgui)) {
        bool enabled = settings::g_advancedTabSettings.disable_dpi_scaling.GetValue();
        LogInfo("Disable DPI scaling setting changed to: %s", enabled ? "true" : "false");

        if (enabled) {
            display_commander::display::dpi::DisableDPIScaling();
        } else {
            display_commander::display::dpi::EnableDPIScaling();
        }
    }
    if (imgui.IsItemHovered()) {
        imgui.SetTooltipEx(
            "Makes the process DPI-aware to prevent Windows from bitmap-scaling the application.\n"
            "Uses AppCompat registry for persistence across restarts.\n"
            "Requires a game restart to take full effect.");
    }

    // Show upgrade status
    if (s_d3d9e_upgrade_successful.load()) {
        imgui.Indent();
        imgui.TextColored(::ui::colors::ICON_SUCCESS, ICON_FK_OK " D3D9 upgraded to D3D9Ex successfully");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Direct3D 9 was successfully upgraded to Direct3D 9Ex.\n"
                "Your game is now using the enhanced D3D9Ex API.");
        }
        imgui.Unindent();
    } else if (settings::g_experimentalTabSettings.d3d9_flipex_enabled.GetValue()
               || settings::g_experimentalTabSettings.d3d9_flipex_enabled_no_reshade.GetValue()) {
        imgui.Indent();
        imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Waiting for D3D9 device creation...");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "The upgrade will occur when the game creates a Direct3D 9 device.\n"
                "If the game is not using D3D9, this setting has no effect.");
        }
        imgui.Unindent();
    }

    imgui.Unindent();
}

void DrawNvapiSettings(display_commander::ui::GraphicsApi api, display_commander::ui::IImGuiWrapper& imgui) {
    uint64_t now_ns = utils::get_now_ns();

    // Minimal NVIDIA Reflex Controls (device runtime dependent); only when Reflex is available (64-bit + native or
    // NVAPI init)
    if (IsReflexAvailable() && imgui.CollapsingHeader("NVIDIA Reflex (Minimal)", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        // Native Reflex Status Indicator
        bool is_native_reflex_active = IsNativeReflexActive();
        if (is_native_reflex_active) {
            imgui.TextColored(::ui::colors::ICON_SUCCESS, ICON_FK_OK " Native Reflex: ACTIVE Limit Real Frames: ON");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("The game has native Reflex support and is actively using it. ");
            }
        } else {
            imgui.TextColored(::ui::colors::TEXT_DIMMED,
                              ICON_FK_MINUS " Native Reflex: INACTIVE Limit Real Frames: OFF");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("No native Reflex activity detected. ");
            }
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
            imgui.SetTooltipEx(
                "Derived from Main tab FPS Limiter Mode and Reflex combo (OnPresent / Reflex / Disabled).");
        }
        imgui.Text("Low Latency: %s", reflex_low_latency ? "Yes" : "No");
        imgui.Text("Boost: %s", reflex_boost ? "Yes" : "No");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Configure in Main tab under FPS Limiter Mode (Reflex combo).");
        }

        /*
        if (imgui.Checkbox("Delay Reflex for first 500 frames", &reflex_delay_first_500_frames)) {
            settings::g_advancedTabSettings.reflex_delay_first_500_frames.SetValue(reflex_delay_first_500_frames);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "When enabled, NVIDIA Reflex integration will not be activated\n"
                "until after the first 500 frames of the game (g_global_frame_id >= 500),\n"
                "even if Reflex (from Main tab) or auto-configure would normally turn it on.");
        }

        if (imgui.Checkbox("Auto Configure Reflex", &reflex_auto_configure)) {
            settings::g_advancedTabSettings.reflex_auto_configure.SetValue(reflex_auto_configure);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx("Automatically configure Reflex settings on startup");
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
                imgui.SetTooltipEx("Tell NVIDIA Reflex to use markers for optimization");
            }

            if (imgui.Checkbox("Generate Reflex Markers", &reflex_generate_markers)) {
                settings::g_advancedTabSettings.reflex_generate_markers.SetValue(reflex_generate_markers);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Generate markers in the frame timeline for latency measurement");
            }
            // Warning about enabling Reflex when game already has it
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_generate_markers.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(::ui::colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Generate Reflex Markers' if the game already has built-in "
                                  "Reflex support!");
            }

            if (imgui.Checkbox("Enable Reflex Sleep Mode", &reflex_enable_sleep)) {
                settings::g_advancedTabSettings.reflex_enable_sleep.SetValue(reflex_enable_sleep);
            }
            if (is_native_reflex_active && settings::g_advancedTabSettings.reflex_enable_sleep.GetValue()) {
                imgui.SameLine();
                imgui.TextColored(::ui::colors::ICON_WARNING, ICON_FK_WARNING
                                  " Warning: Do not enable 'Enable Reflex Sleep Mode' if the game already has "
                                  "built-in Reflex support!");
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Enable Reflex sleep mode calls (disabled by default for safety).");
            }
            if (reflex_auto_configure) {
                imgui.EndDisabled();
            }
            bool reflex_logging = settings::g_advancedTabSettings.reflex_logging.GetValue();
            if (imgui.Checkbox("Enable Reflex Logging", &reflex_logging)) {
                settings::g_advancedTabSettings.reflex_logging.SetValue(reflex_logging);
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Enable detailed logging of Reflex marker operations for debugging purposes.");
            }
        }
*/
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

            if (!g_reflexProvider) {
                unavailable_reason = SleepStatusUnavailableReason::kNoReflex;
            } else if (!g_reflexProvider->IsInitialized()) {
                unavailable_reason = SleepStatusUnavailableReason::kReflexNotInitialized;
            } else {
                status_available = g_reflexProvider->GetSleepStatus(&sleep_status, &unavailable_reason);
            }

            if (status_available) {
                imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Current Reflex Status:");
                imgui.Indent();

                // Low Latency Mode status
                bool low_latency_enabled = (sleep_status.bLowLatencyMode == NV_TRUE);
                imgui.TextColored(low_latency_enabled ? ::ui::colors::ICON_SUCCESS : ::ui::colors::TEXT_DIMMED,
                                  "Low Latency Mode: %s", low_latency_enabled ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Indicates whether NVIDIA Reflex Low Latency Mode is currently active in the driver.");
                }

                // Fullscreen VRR status
                bool fs_vrr = (sleep_status.bFsVrr == NV_TRUE);
                imgui.Text("Fullscreen VRR: %s", fs_vrr ? "ENABLED" : "DISABLED");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Indicates if fullscreen GSYNC or GSYNC Compatible mode is active (valid only when app is in "
                        "foreground).");
                }

                // Control Panel VSYNC Override
                bool cpl_vsync_on = (sleep_status.bCplVsyncOn == NV_TRUE);
                imgui.Text("Control Panel VSYNC Override: %s", cpl_vsync_on ? "ON" : "OFF");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Indicates if NVIDIA Control Panel is overriding VSYNC settings.");
                }

                // Sleep interval
                if (sleep_status.sleepIntervalUs > 0) {
                    float fps_limit = 1000000.0f / static_cast<float>(sleep_status.sleepIntervalUs);
                    imgui.Text("Sleep Interval: %u us (%.2f FPS limit)", sleep_status.sleepIntervalUs, fps_limit);
                } else {
                    imgui.Text("Sleep Interval: Not set");
                }
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Latest sleep interval in microseconds (inverse of FPS limit).");
                }

                // Game Sleep status
                bool use_game_sleep = (sleep_status.bUseGameSleep == NV_TRUE);
                imgui.Text("Game Sleep Calls: %s", use_game_sleep ? "ACTIVE" : "INACTIVE");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Indicates if NvAPI_D3D_Sleep() is being called by the game or addon.");
                }

                imgui.Unindent();
            } else {
                imgui.TextColored(::ui::colors::TEXT_DIMMED, "Sleep status not available: %s",
                                  SleepStatusUnavailableReasonToString(unavailable_reason));
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "Sleep status requires an initialized DirectX 11/12 device and NVIDIA GPU with Reflex "
                        "support.");
                }
            }

            // NvLL VK (Vulkan Reflex) params when NvLowLatencyVk hooks are active
            if (AreNvLowLatencyVkHooksInstalled()) {
                imgui.Spacing();
                imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "NvLL VK (Vulkan Reflex) SetSleepMode:");
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx(
                        "When NvLowLatencyVk hooks are installed, we re-apply SleepMode on SIMULATION_START.\n"
                        "'Last applied' is what we sent to the driver; 'Game tried to set' is what the game passed.");
                }
                imgui.Indent();
                NvLLVkSleepModeParamsView last_applied = {};
                GetNvLowLatencyVkLastAppliedSleepModeParams(&last_applied);
                if (last_applied.has_value) {
                    imgui.TextColored(::ui::colors::ICON_SUCCESS, "Last applied (via SetSleepMode_Original):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               last_applied.low_latency ? "Yes" : "No", last_applied.boost ? "Yes" : "No",
                               last_applied.minimum_interval_us);
                    if (last_applied.minimum_interval_us > 0) {
                        float fps = 1000000.0f / last_applied.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "Last applied: (none yet)");
                }
                NvLLVkSleepModeParamsView game_params = {};
                GetNvLowLatencyVkGameSleepModeParams(&game_params);
                if (game_params.has_value) {
                    imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Game tried to set (NvLL_VK_SetSleepMode):");
                    imgui.Text("  Low Latency: %s  Boost: %s  Min interval: %u us",
                               game_params.low_latency ? "Yes" : "No", game_params.boost ? "Yes" : "No",
                               game_params.minimum_interval_us);
                    if (game_params.minimum_interval_us > 0) {
                        float fps = 1000000.0f / game_params.minimum_interval_us;
                        imgui.Text("  Target FPS: %.1f", fps);
                    }
                } else {
                    imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "Game tried to set: (none yet)");
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

            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Reflex API Call Counters:");
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
            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Individual Marker Type Counts:");
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
            imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f}, "These counters help debug Reflex FPS limiter issues.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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

            imgui.TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.0f}, "Native Reflex API Call Counters:");
            imgui.Indent();
            imgui.Text("NvAPI_D3D_Sleep calls: %u", native_sleep_count);
            if (native_sleep_count > 0 && native_sleep_ns_smooth > 0) {
                double native_calls_per_second = 1000000000.0 / static_cast<double>(native_sleep_ns_smooth);
                imgui.Text("Native Sleep Rate: %.2f times/sec (%.1f ms interval)", native_calls_per_second,
                           native_sleep_ns_smooth / 1000000.0);
                if (imgui.IsItemHovered()) {
                    imgui.SetTooltipEx("Smoothed interval using rolling average. Raw: %.1f ms",
                                       native_sleep_ns > 0 ? native_sleep_ns / 1000000.0 : 0.0);
                }
            }
            imgui.Text("NvAPI_D3D_SetSleepMode calls: %u", native_set_sleep_mode_count);
            imgui.Text("NvAPI_D3D_SetLatencyMarker calls: %u", native_set_latency_marker_count);
            imgui.Text("NvAPI_D3D_GetLatency calls: %u", native_get_latency_count);
            imgui.Text("NvAPI_D3D_GetSleepStatus calls: %u", native_get_sleep_status_count);
            imgui.Unindent();

            imgui.Spacing();
            imgui.TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.0f},
                              "These counters track native Reflex API calls from the game.");
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx(
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
                ::g_nvapi_d3d_last_sleep_global_frame_id.store(0);
                for (size_t i = 0; i < static_cast<size_t>(::kLatencyMarkerTypeCountFirstSix); ++i) {
                    ::g_nvapi_d3d_last_global_frame_id_by_marker_type[i].store(0);
                }
            }
            if (imgui.IsItemHovered()) {
                imgui.SetTooltipEx("Reset all Reflex debug counters to zero.");
            }
        }
        imgui.Unindent();
    }

    // Fake NVAPI Settings
    imgui.Spacing();
    if (imgui.CollapsingHeader("AntiLag 2 / XeLL support (fakenvapi / custom nvapi64.dll)",
                               wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();
        imgui.TextColored(::ui::colors::TEXT_WARNING, "Load AL2/AL+/XeLL through nvapi64.dll");

        bool fake_nvapi_enabled = settings::g_advancedTabSettings.fake_nvapi_enabled.GetValue();
        if (imgui.Checkbox("Enable (requires restart)", &fake_nvapi_enabled)) {
            settings::g_advancedTabSettings.fake_nvapi_enabled.SetValue(fake_nvapi_enabled);
            settings::g_advancedTabSettings.fake_nvapi_enabled.Save();
            s_restart_needed_nvapi.store(true);
        }
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "AntiLag 2, Vulkan AntiLag+ or XeLL are automatically selected when available.\n"
                "Add nvapi64.dll to the addon directory (rename fakenvapi.dll if needed).\n\n"
                "Downlaod from here: https://github.com/emoose/fakenvapi\n");
        }
        if (s_restart_needed_nvapi.load()) {
            imgui.Spacing();
            imgui.TextColored(::ui::colors::TEXT_ERROR, "Game restart required to apply NVAPI changes.");
        }

        imgui.Unindent();  // Unindent nested header section

        // Warning about experimental nature
        imgui.Spacing();
        imgui.TextColored(::ui::colors::TEXT_WARNING, ICON_FK_WARNING " Experimental Feature");
        if (imgui.IsItemHovered()) {
            imgui.SetTooltipEx(
                "Fake NVAPI is experimental and may cause:\n"
                "- Game crashes or instability\n"
                "- Performance issues\n"
                "- Incompatibility with some games\n\n"
                "Use at your own risk!");
        }
        imgui.Unindent();
    }

    // Unsupported/unfinished features
    imgui.Spacing();
    if (imgui.CollapsingHeader("Unsupported/unfinished features", wrapper_flags::TreeNodeFlags_None)) {
        imgui.Indent();

        // HDR and Display Settings (under unsupported)
        if (imgui.CollapsingHeader("HDR and Display Settings", wrapper_flags::TreeNodeFlags_None)) {
            DrawHdrDisplaySettings(api, imgui);
        }

        imgui.Unindent();
    }
}

}  // namespace ui::new_ui
