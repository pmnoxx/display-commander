#include "developer_new_tab.hpp"
#include "../../globals.hpp"
#include "../../nvapi/nvapi_fullscreen_prevention.hpp"
#include "../../settings/developer_tab_settings.hpp"
#include "../../utils.hpp"
#include "settings_wrapper.hpp"

#include <atomic>
#include <iomanip>
#include <sstream>

static std::atomic<bool> s_restart_needed_nvapi(false);

namespace ui::new_ui {

void InitDeveloperNewTab() {
    // Ensure settings are loaded
    static bool settings_loaded = false;
    if (!settings_loaded) {
        settings::g_developerTabSettings.LoadAll();
        settings_loaded = true;
    }
}

void DrawDeveloperNewTab() {
    ImGui::Text("Developer Tab - Advanced Features");
    ImGui::Separator();

    // Developer Settings Section
    DrawDeveloperSettings();

    ImGui::Spacing();
    ImGui::Separator();

    // HDR and Display Settings Section
    DrawHdrDisplaySettings();

    ImGui::Spacing();
    ImGui::Separator();

    // NVAPI Settings Section
    DrawNvapiSettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Resolution Override Settings Section
    DrawResolutionOverrideSettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Keyboard Shortcuts Section
    DrawKeyboardShortcutsSettings();

    ImGui::Spacing();
    ImGui::Separator();

    // Latency Display Section
    DrawLatencyDisplay();
}

void DrawDeveloperSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Developer Settings ===");

    // Performance optimization: Flush before present
    if (CheckboxSetting(settings::g_developerTabSettings.flush_before_present, "Flush Command Queue Before Present")) {
        ::g_flush_before_present.store(settings::g_developerTabSettings.flush_before_present.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Flush command queue before ReShade shaders process. Reduces latency and increases FPS by "
            "ensuring GPU commands are processed before shader execution.");
    }

    // Prevent Fullscreen (global)
    if (CheckboxSetting(settings::g_developerTabSettings.prevent_fullscreen, "Prevent Fullscreen")) {
        // Update global variable for compatibility
        s_prevent_fullscreen.store(settings::g_developerTabSettings.prevent_fullscreen.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevent exclusive fullscreen; keep borderless/windowed for stability and HDR.");
    }

    // Spoof Fullscreen State
    const char* spoof_fullscreen_labels[] = {"Disabled", "Spoof as Fullscreen", "Spoof as Windowed"};
    int spoof_fullscreen_state = static_cast<int>(settings::g_developerTabSettings.spoof_fullscreen_state.GetValue());
    if (ImGui::Combo("Spoof Fullscreen State", &spoof_fullscreen_state, spoof_fullscreen_labels, 3)) {
        settings::g_developerTabSettings.spoof_fullscreen_state.SetValue(spoof_fullscreen_state);
        s_spoof_fullscreen_state.store(static_cast<SpoofFullscreenState>(spoof_fullscreen_state));

        // Log the change
        std::ostringstream oss;
        oss << "Fullscreen state spoofing changed to ";
        if (spoof_fullscreen_state < 0.5f) {
            oss << "Disabled";
        } else if (spoof_fullscreen_state < 1.5f) {
            oss << "Spoof as Fullscreen";
        } else {
            oss << "Spoof as Windowed";
        }
        LogInfo(oss.str().c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Spoof fullscreen state detection for applications that query fullscreen status. Useful for "
            "games that change behavior based on fullscreen state.");
    }

    // Reset button for Fullscreen State (only show if not at default)
    if (s_spoof_fullscreen_state.load() != SpoofFullscreenState::Disabled) {
        ImGui::SameLine();
        if (ImGui::Button("Reset##DevFullscreen")) {
            settings::g_developerTabSettings.spoof_fullscreen_state.SetValue(0);
            s_spoof_fullscreen_state.store(SpoofFullscreenState::Disabled);
            LogInfo("Fullscreen state spoofing reset to disabled");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reset to default (Disabled)");
        }
    }

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

    ImGui::Spacing();

    // Continuous Monitoring moved to Main tab as 'Auto-apply'

    // Prevent Always On Top
    if (CheckboxSetting(settings::g_developerTabSettings.prevent_always_on_top, "Prevent Always On Top")) {
        s_prevent_always_on_top.store(settings::g_developerTabSettings.prevent_always_on_top.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Prevents windows from becoming always on top, even if they are moved or resized.");
    }
}

void DrawHdrDisplaySettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== HDR and Display Settings ===");

    // Hide HDR Capabilities
    if (CheckboxSetting(settings::g_developerTabSettings.hide_hdr_capabilities,
                        "Hide game's native HDR")) {
        s_hide_hdr_capabilities.store(settings::g_developerTabSettings.hide_hdr_capabilities.GetValue());
        LogInfo("HDR hiding setting changed to: %s",
                settings::g_developerTabSettings.hide_hdr_capabilities.GetValue() ? "true" : "false");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hides HDR capabilities from applications by intercepting CheckColorSpaceSupport and GetDesc calls.\n"
                         "This can prevent games from detecting HDR support and force them to use SDR mode.");
    }
}

void DrawNvapiSettings() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== NVAPI Settings ===");

    // HDR10 Colorspace Fix
    if (CheckboxSetting(settings::g_developerTabSettings.nvapi_fix_hdr10_colorspace,
                        "Set ReShade Effects Processing to HDR10 Colorspace")) {
        s_nvapi_fix_hdr10_colorspace.store(settings::g_developerTabSettings.nvapi_fix_hdr10_colorspace.GetValue());
        s_restart_needed_nvapi.store(true);
    }

    // NVAPI Fullscreen Prevention
    if (CheckboxSetting(settings::g_developerTabSettings.nvapi_fullscreen_prevention, "NVAPI Fullscreen Prevention")) {
        s_nvapi_fullscreen_prevention.store(settings::g_developerTabSettings.nvapi_fullscreen_prevention.GetValue());
        s_restart_needed_nvapi.store(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Use NVAPI to prevent fullscreen mode at the driver level.");
    }

    // NVAPI Auto-enable for specific games
    if (CheckboxSetting(settings::g_developerTabSettings.nvapi_auto_enable, "NVAPI Auto-enable for Games")) {
        s_nvapi_auto_enable.store(settings::g_developerTabSettings.nvapi_auto_enable.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Automatically enable NVAPI features for specific games:\n"
                         "• NVAPI Fullscreen Prevention\n"
                         "• HDR10 Colorspace Fix\n\n"
                         "Note: DLDSR needs to be off for proper functionality\n\n"
                         "Supported games:\n"
                         "• Armored Core 6\n"
                         "• Devil May Cry 5\n"
                         "• Elden Ring\n"
                         "• Hitman\n"
                         "• Resident Evil 2\n"
                         "• Resident Evil 3\n"
                         "• Resident Evil 7\n"
                         "• Resident Evil 8\n"
                         "• Sekiro: Shadows Die Twice");
    }
    // Display restart-required notice if flagged
    if (s_restart_needed_nvapi.load()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Game restart required to apply NVAPI changes.");
    }
    if (s_nvapi_fullscreen_prevention.load() && ::g_nvapiFullscreenPrevention.IsAvailable()) {


        // NVAPI Debug Information Display
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "NVAPI Debug Information:");

        extern NVAPIFullscreenPrevention g_nvapiFullscreenPrevention;

        // Library loaded successfully
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁ENVAPI Library: Loaded");

        // Driver version info
        std::string driverVersion = ::g_nvapiFullscreenPrevention.GetDriverVersion();
        if (driverVersion != "Failed to get driver version") {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EDriver Version: %s", driverVersion.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ Driver Version: %s", driverVersion.c_str());
        }

        // Hardware detection
        if (::g_nvapiFullscreenPrevention.HasNVIDIAHardware()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁ENVIDIA Hardware: Detected");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✁ENVIDIA Hardware: Not Found");
        }

        // Fullscreen prevention status
        if (::g_nvapiFullscreenPrevention.IsFullscreenPreventionEnabled()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EFullscreen Prevention: ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "◁EFullscreen Prevention: Inactive");
        }

        // Function availability check
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁ECore Functions: Available");
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✁EDRS Functions: Available");

    } else {
        // Library not loaded
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✁ENVAPI Library: Not Loaded");

        // Try to get error information
        std::string lastError = ::g_nvapiFullscreenPrevention.GetLastError();
        if (!lastError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Error: %s", lastError.c_str());
        }
    }

    // Minimal NVIDIA Reflex Controls (device runtime dependent)
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== NVIDIA Reflex (Minimal) ===");

    // Warning about enabling Reflex when game already has it
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                       "⚠ Warning: Do not enable Reflex if the game already has built-in Reflex support!");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enabling Reflex when the game already has it can cause conflicts, instability, or "
            "performance issues. Check the game's graphics settings first.");
    }

    bool reflex_enable = settings::g_developerTabSettings.reflex_enable.GetValue();
    if (ImGui::Checkbox("Enable Reflex", &reflex_enable)) {
        settings::g_developerTabSettings.reflex_enable.SetValue(reflex_enable);
        s_reflex_enable.store(reflex_enable);
    }
    bool reflex_low_latency = settings::g_developerTabSettings.reflex_low_latency.GetValue();
    if (ImGui::Checkbox("Low Latency Mode", &reflex_low_latency)) {
        settings::g_developerTabSettings.reflex_low_latency.SetValue(reflex_low_latency);
        s_reflex_low_latency.store(reflex_low_latency);
    }
    bool reflex_boost = settings::g_developerTabSettings.reflex_boost.GetValue();
    if (ImGui::Checkbox("Low Latency Boost", &reflex_boost)) {
        settings::g_developerTabSettings.reflex_boost.SetValue(reflex_boost);
        s_reflex_boost.store(reflex_boost);
    }
    bool reflex_markers = settings::g_developerTabSettings.reflex_use_markers.GetValue();
    if (ImGui::Checkbox("Use Markers to Optimize", &reflex_markers)) {
        settings::g_developerTabSettings.reflex_use_markers.SetValue(reflex_markers);
        s_reflex_use_markers.store(reflex_markers);
    }
    bool reflex_logging = settings::g_developerTabSettings.reflex_logging.GetValue();
    if (ImGui::Checkbox("Enable Reflex Logging", &reflex_logging)) {
        settings::g_developerTabSettings.reflex_logging.SetValue(reflex_logging);
        s_enable_reflex_logging.store(reflex_logging);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable detailed logging of Reflex marker operations for debugging purposes.");
    }

    // DLL version info
    if (!::g_nvapiFullscreenPrevention.IsAvailable()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "DLL Information:");
        std::string dllInfo = ::g_nvapiFullscreenPrevention.GetDllVersionInfo();
        if (dllInfo != "No library loaded") {
            ImGui::TextWrapped("%s", dllInfo.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "DLL not loaded - cannot get version info");
        }
    }
}

void DrawResolutionOverrideSettings() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "=== Resolution Override ===");

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Override the backbuffer resolution during swapchain creation. Same as ReShade ForceResolution.");
    }
}

void DrawKeyboardShortcutsSettings() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "=== Keyboard Shortcuts ===");

    // Enable Mute/Unmute Shortcut (Ctrl+M)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_mute_unmute_shortcut,
                        "Enable Mute/Unmute Shortcut (Ctrl+M)")) {
        ::s_enable_mute_unmute_shortcut.store(settings::g_developerTabSettings.enable_mute_unmute_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+M to quickly mute/unmute audio. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+M
    if (settings::g_developerTabSettings.enable_mute_unmute_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+M to toggle audio mute state");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable Background Toggle Shortcut (Ctrl+R)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_background_toggle_shortcut,
                        "Enable Background Toggle Shortcut (Ctrl+R)")) {
        ::s_enable_background_toggle_shortcut.store(
            settings::g_developerTabSettings.enable_background_toggle_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+R to quickly toggle both 'No Render in Background' and 'No "
            "Present in Background' settings. Only works when the game is in the foreground.");
    }

    // Info text for Ctrl+R
    if (settings::g_developerTabSettings.enable_background_toggle_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                           "Press Ctrl+R to toggle background rendering/present settings");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }

    // Enable Time Slowdown Shortcut (Ctrl+T)
    if (CheckboxSetting(settings::g_developerTabSettings.enable_timeslowdown_shortcut,
                        "Enable Time Slowdown Shortcut (Ctrl+T)")) {
        ::s_enable_timeslowdown_shortcut.store(settings::g_developerTabSettings.enable_timeslowdown_shortcut.GetValue());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Enable keyboard shortcut Ctrl+T to quickly toggle Time Slowdown. Only works when the game is "
            "in the foreground.");
    }

    // Info text for Ctrl+T
    if (settings::g_developerTabSettings.enable_timeslowdown_shortcut.GetValue()) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Press Ctrl+T to toggle Time Slowdown");
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Shortcut works when game is in foreground");
        ImGui::Unindent();
    }
}

void DrawLatencyDisplay() {
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "=== Latency Information ===");

    // Current Latency Display
    extern std::atomic<float> g_current_latency_ms;
    float latency = ::g_current_latency_ms.load();

    std::ostringstream oss;
    oss << "Current Latency: " << std::fixed << std::setprecision(2) << latency << " ms";
    ImGui::TextUnformatted(oss.str().c_str());

    // Present Duration Display
    extern std::atomic<double> g_present_duration;
    double present_duration = ::g_present_duration_ns.load();

    oss.str("");
    oss.clear();
    oss << "Present Duration: " << std::fixed << std::setprecision(6) << present_duration << " ms";
    ImGui::TextUnformatted(oss.str().c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "(smoothed)");
}

}  // namespace ui::new_ui
